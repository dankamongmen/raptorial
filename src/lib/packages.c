#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <blossom.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <raptorial.h>

// For now, the datastore is a DAG; pkgobjs do not link to their containing pkgcaches,
// and pkgcaches do not link to their containing releases. This might not fly once we
// need compare multiple versions of a package across several pkgcaches, as we'd then
// need do a search on each of N caches. So long as we can find a pkgobj in a pkgcache
// quickly enough, though, this isn't necessarily a problem. We'll thus definitely
// need at least lgN search on pkgcaches; this is simple enough, though. Right now
// we're just using linked lists, which will definitely have to go FIXME.
typedef struct pkgobj {
	struct pkgobj *next;
	char *name;
	char *status;
	char *version;
} pkgobj;

// One package cache per Packages/Sources file. A release will generally have
// { |Architectures| X |Components| } Packages files, and one Sources file per
// component.
typedef struct pkglist {
	pkgobj *pobjs;
	unsigned pcount;
	struct pkglist *next;
	char *uri,*arch,*distribution;
} pkglist;

// Contains >= 1 components and >= 1 architectures. Parameterized by (at least)
// origin, label, and codename.
typedef struct release {
} release;

// For now, just a flat list of pkglists; we'll likely introduce structure.
typedef struct pkgcache {
	pkglist *lists;
} pkgcache;

struct pkgparse {
	unsigned count;
	const void *mem;
	size_t len,csize;
	// Are we a status file, or a package list?
	int statusfile;
	pthread_mutex_t lock;

	// These data are modified by threads, and must be protected by the
	// lock. A thread takes the chunk at offset, incrementing offset by
	// the chunksize. Parsed pkgobjs are placed in sharedpcache.
	size_t offset;
	pkglist *sharedpcache;
};

static void
free_package(pkgobj *po){
	free(po->version);
	free(po->status);
	free(po->name);
	free(po);
}

static pkgobj *
create_package(const char *name,size_t namelen,const char *ver,size_t verlen,
			const char *status,size_t statuslen){
	pkgobj *po;

	if( (po = malloc(sizeof(*po))) ){
		if(ver){
			if((po->version = malloc(sizeof(*po->version) * (verlen + 1))) == NULL){
				free(po);
				return NULL;
			}
			strncpy(po->version,ver,verlen);
			po->version[verlen] = '\0';
		}else{
			po->version = NULL;
		}
		if((po->name = malloc(sizeof(*po->name) * (namelen + 1))) == NULL){
			free(po->version);
			free(po);
			return NULL;
		}
		if(status){
			if((po->status = malloc(sizeof(*po->status) * (statuslen + 1))) == NULL){
				free(po->name);
				free(po->version);
				free(po);
				return NULL;
			}
			strncpy(po->status,status,statuslen);
			po->status[statuslen] = '\0';
		}else{
			po->status = NULL;
		}
		strncpy(po->name,name,namelen);
		po->name[namelen] = '\0';
	}
	return po;
}

static size_t
get_new_offset(struct pkgparse *pp){
	size_t o;

	pthread_mutex_lock(&pp->lock);
	o = pp->offset;
	if(pp->len - pp->offset < pp->csize){ // Avoids possible overflow
		pp->offset = pp->len;
	}else{
		pp->offset += pp->csize;
	}
	pthread_mutex_unlock(&pp->lock);
	return o;
}

enum {
	STATE_PDATA = 0,
	STATE_NLINE = 1,
	STATE_RESET = 2,
	STATE_EXPECT,
	STATE_DELIM,
	STATE_VERSION_DELIMITED,
	STATE_PACKAGE_DELIMITED,
	STATE_STATUS_DELIMITED,
};

// Threads handle chunks of the packages list. Since we don't know where
// package definitions are split in the list data, we'll usually toss some data
// from the front (it was handled as part of another chunk), and grab some
// extra data from the back. These will typically be small amounts, and the
// data is read-only, so rather than track splits (requiring synchronization),
// we just always locally discover the bounds (put another way, the overlapping
// areas at the front and back of chunks are lexed twice).
//
// Returns the number of packages parsed (possibly 0), or -1 on error. In the
// case of an error, packages already parsed *are not* freed, and enq *is
// not* reset.
//
// We're not treating the input as anything but ASCII text, though it's almost
// certainly UTF8. Need to ensure that newlines and pattern tags are all
// strictly ASCII or change how we handle things FIXME.
static int
parse_chunk(size_t offset,const char *start,const char *end,
		const char *veryend,pkgobj ***enq,int statusfile){
	const char *expect,*pname,*pver,*pstatus,*c,*delim;
	size_t pnamelen,pverlen,pstatuslen;
	int rewardstate,state;
	unsigned newp = 0;
	pkgobj *po;

	// First, find the start of our chunk:
	//  - If we are offset 0, we are at the start of our chunk
	//  - Otherwise, if the previous two characters (those preceding our
	//     chunk) are newlines, we are at the start of our chunk,
	//  - Otherwise, if the first character is a newline, and the previous
	//     character is a newline, we are at the start of our chunk
	//  - Otherwise, our chunk starts at the first double newline
	if(offset){
		// We can be in one of two states: we know the previous
		// character to have been a newline, or we don't.
		state = STATE_PDATA;
		//assert(pp->csize > 2); // sanity check
		for(c = start - 2 ; c < end ; ++c){
			if(*c == '\n'){
				if(state == STATE_NLINE){
					++c;
					break;
				}
				state = STATE_NLINE;
			}else{
				state = STATE_PDATA;
			}
		}
	}else{
		c = start;
	}
	// We are at the beginning of our chunk, which might be 0 bytes. Any
	// partial record with which our map started has been skipped
	// Upon reaching the (optional, only one allowed) delimiter on each
	// line, delim will be updated to point one past that delimiter (which
	// might be outside the chunk!), and to chew whitespace.
	delim = NULL;
	expect = NULL;
	// We hand create_package our raw map bytes; it allocates the destbuf.
	// These are thus reset on each package.
	pname = NULL; pver = NULL; pstatus = NULL;
	state = STATE_RESET; // number of newlines we've seen, bounded by 2
	rewardstate = STATE_RESET;
	pstatuslen = pverlen = pnamelen = 0;
	while(c < end || (state != STATE_RESET && c < veryend)){
		if(*c == '\n'){ // State machine is driven by newlines
			switch(state){
			case STATE_NLINE:{ // double newline
				if(pname == NULL || pnamelen == 0){
					return -1; // No package name
				}
				if(statusfile){
					if(pstatus == NULL || pstatuslen == 0){
						return -1; // No package status
					}
				}else{
				       	if(pstatus){
						return -1; // Status in package list
					}
					if(pver == NULL || pverlen == 0){
						return -1; // No package version
					}
				}
				if((po = create_package(pname,pnamelen,pver,pverlen,
								pstatus,pstatuslen)) == NULL){
					return -1;
				}
				// Package ended!
				++newp;
				**enq = po;
				*enq = &po->next;
				pname = NULL;
				pver = NULL;
				pstatus = NULL;
				state = STATE_RESET;
				break;
			} // Else we processed a line of the current package
			case STATE_PACKAGE_DELIMITED:
// Don't allow a package to be named twice. Defined another way, require an
// empty line between every two instances of a Package: line.
				if(pname){
					return -1;
				}
				pnamelen = c - delim;
				pname = delim;
				state = STATE_NLINE;
				break;
			case STATE_VERSION_DELIMITED:
				if(pver){
					return -1;
				}
				pverlen = c - delim;
				pver = delim;
				state = STATE_NLINE;
				break;
			case STATE_STATUS_DELIMITED:
				if(pstatus){
					return -1;
				}
				pstatuslen = c - delim;
				pstatus = delim;
				break;
			}
			if(state != STATE_RESET){
				state = STATE_NLINE;
			}
		}else switch(state){ // not a newline
			case STATE_NLINE:
			case STATE_RESET:
				delim = NULL;
				start = c;
				if(*c == 'V'){
					state = STATE_EXPECT;
					expect = "ersion:";
					rewardstate = STATE_VERSION_DELIMITED;
				}else if(*c == 'P'){
					state = STATE_EXPECT;
					expect = "ackage:";
					rewardstate = STATE_PACKAGE_DELIMITED;
				}else if(*c == 'S'){
					state = STATE_EXPECT;
					expect = "tatus:";
					rewardstate = STATE_STATUS_DELIMITED;
				}else{
					state = STATE_PDATA;
				}
				break;
			case STATE_EXPECT:
				if(*c == *expect){
					if(!*++expect){
						state = STATE_DELIM;
						delim = c + 1;
					}
				}else{
					state = STATE_PDATA;
				}
				break;
			case STATE_DELIM:
				if(isspace(*c)){
					++delim;
				}else{
					state = rewardstate;
				}
				break;
		}
		++c;
	}
	if(state != STATE_RESET){
		return -1;
	}
	return newp;
}

static void *
parse_chunks(void *vpp){
	const char *start,*end,*veryend;
	struct pkgparse *pp = vpp;
	pkgobj *head,**enq,*po;
	unsigned packages = 0;
	size_t offset;

	head = NULL;
	enq = &head;
	offset = get_new_offset(pp);
	// We can go past the end of our chunk to finish a package's parsing
	// in media res, but we can't go past the end of the actual map!
	veryend = (const char *)pp->mem + pp->len;
	while(offset < pp->len){
		int newp;

		start = (const char *)pp->mem + offset;
		if(pp->csize + offset > pp->len){
			end = start + (pp->len - offset);
		}else{
			end = start + pp->csize;
		}
		newp = parse_chunk(offset,start,end,veryend,&enq,pp->statusfile);
		if(newp < 0){
			goto err;
		}
		packages += newp;
		offset = get_new_offset(pp);
	}
	if(head){
		pthread_mutex_lock(&pp->lock); // Success!
			pp->sharedpcache->pcount += packages;
			*enq = pp->sharedpcache->pobjs;
			pp->sharedpcache->pobjs = head;
		pthread_mutex_unlock(&pp->lock);
	}
	return vpp;

err:
	while( (po = head) ){
		head = po->next;
		free_package(po);
	}
	return NULL;
}

struct dirparse {
	DIR *dir;
	pkgcache *sharedpcache;
	pthread_mutex_t lock;
};

// len is the true length, less than or equal to the mapped length.
static int
parse_map(pkglist *pl,const void *mem,size_t len,int *err,int statusfile){
	struct pkgparse pp = {
		.statusfile = statusfile,
		.mem = mem,
		.len = len,
		.offset = 0,
		.sharedpcache = pl,
		.csize = 1024 * 1024,
	};
	blossom_ctl bctl = {
		.flags = 0,
		.tids = 1,
	};
	blossom_state bs;
	int r;

	if( (r = pthread_mutex_init(&pp.lock,NULL)) ){
		*err = r;
		return -1;
	}
	if(blossom_per_pe(&bctl,&bs,NULL,parse_chunks,&pp)){
		*err = errno;
		pthread_mutex_destroy(&pp.lock);
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		pthread_mutex_destroy(&pp.lock);
		return -1;
	}
	if(blossom_validate_joinrets(&bs)){
		blossom_free_state(&bs);
		pthread_mutex_destroy(&pp.lock);
		return -1;
	}
	blossom_free_state(&bs);
	if((r = pthread_mutex_destroy(&pp.lock))){
		*err = r;
		return -1;
	}
	return 0;
}

static inline pkglist *
create_pkglist(const void *mem,size_t len,int *err,int statusfile){
	pkglist *pl;

	if((pl = malloc(sizeof(*pl))) == NULL){
		*err = errno;
	}else{
		memset(pl,0,sizeof(*pl));
		if(parse_map(pl,mem,len,err,statusfile)){
			free(pl);
			return NULL;
		}
	}
	return pl;
}

static inline pkgcache *
create_pkgcache(pkglist *pl,int *err){
	pkgcache *pc;

	if((pc = malloc(sizeof(*pc))) == NULL){
		*err = errno;
	}else{
		pc->lists = pl;
	}
	return pc;
}

static pkglist *
parse_packages_file_internal(const char *path,int *err,int statusfile){
	const void *map;
	size_t mlen,len;
	struct stat st;
	pkglist *pl;
	int fd,pg;

	if(path == NULL){
		*err = EINVAL;
		return NULL;
	}
	// Probably ought get the largest page size; this will be the smallest
	// on all platforms of which I'm aware. FIXME
	if((pg = sysconf(_SC_PAGE_SIZE)) <= 0){
		*err = errno;
		return NULL;
	}
	if((fd = open(path,O_CLOEXEC)) < 0){
		*err = errno;
		return NULL;
	}
	if(fstat(fd,&st)){
		*err = errno;
		close(fd);
		return NULL;
	}
	mlen = len = st.st_size;
	if(mlen % pg != mlen / pg){
		mlen = (mlen / pg) * pg + pg;
	}
	if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_HUGETLB|MAP_POPULATE,fd,0)) == MAP_FAILED){
		if(errno != EINVAL){
			*err = errno;
			close(fd);
			return NULL;
		}
		// Try again without MAP_HUGETLB
		if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_POPULATE,fd,0)) == MAP_FAILED){
			*err = errno;
			close(fd);
			return NULL;
		}
	}
	if((pl = create_pkglist(map,len,err,statusfile)) == NULL){
		close(fd);
		return NULL;
	}
	close(fd);
	return pl;
}

PUBLIC pkglist *
parse_packages_file(const char *path,int *err){
	return parse_packages_file_internal(path,err,0);
}

PUBLIC pkglist *
parse_packages_mem(const void *mem,size_t len,int *err){
	pkglist *pl;

	if(mem == NULL || len == 0){
		*err = EINVAL;
		return NULL;
	}
	if((pl = create_pkglist(mem,len,err,0)) == NULL){
		return NULL;
	}
	return pl;
}

PUBLIC void
free_package_list(pkglist *pl){
	if(pl){
		free(pl->distribution);
		free(pl->arch);
		free(pl->uri);
		free(pl);
	}
}

PUBLIC pkgcache *
pkgcache_from_pkglist(pkglist *pl,int *err){
	pkgcache *pc;

	if(pl == NULL){
		return NULL;
	}
	if((pc = create_pkgcache(pl,err)) == NULL){
		free_package_list(pl);
	}
	return pc;
}

PUBLIC void
free_package_cache(pkgcache *pc){
	if(pc){
		pkglist *pl;

		while( (pl = pc->lists) ){
			pc->lists = pl->next;
			free_package_list(pl);
		}
		free(pc);
	}
}

PUBLIC pkglist *
parse_status_file(const char *path,int *err){
	return parse_packages_file_internal(path,err,1);
}

PUBLIC pkglist *
pkgcache_begin(pkgcache *pc){
	return pc->lists;
}

PUBLIC const pkglist *
pkgcache_next(const pkglist *pl){
	return pl->next;
}

PUBLIC const char *
pkglist_uri(const pkglist *pl){
	return pl->uri;
}

PUBLIC const pkgobj *
pkglist_begin(const pkglist *pl){
	return pl->pobjs;
}

PUBLIC const pkgobj *
pkglist_next(const pkgobj *po){
	return po->next;
}

PUBLIC const char *
pkgobj_name(const pkgobj *po){
	return po->name;
}

PUBLIC const char *
pkgobj_status(const pkgobj *po){
	return po->status;
}

PUBLIC const char *
pkglist_dist(const pkglist *pl){
	return pl->distribution;
}

PUBLIC const char *
pkgobj_version(const pkgobj *po){
	return po->version;
}

PUBLIC unsigned
pkgcache_count(const pkgcache *pc){
	unsigned tot = 0;

	if(pc){
		pkglist *pl;

		for(pl = pc->lists ; pl ; pl = pl ->next){
			tot += pl->pcount;
		}
	}
	return tot;
}

static void *
parse_dir(void *vdp){
	struct dirparse *dp = vdp;
	struct dirent dent,*pdent;

	while(readdir_r(dp->dir,&dent,&pdent) == 0){
		const char *suffixes[] = { "Sources", "Packages", NULL },**suffix;
		const char *distdelim,*dist,*uridelim;
		pkglist *pl;

		if(pdent == NULL){
			return dp;
		}
		if(dent.d_type != DT_REG && dent.d_type != DT_LNK){
			continue; // FIXME maybe don't skip DT_UNKNOWN?
		}
		if((uridelim = strchr(dent.d_name,'_')) == NULL){
			continue;
		}
		if((dist = strstr(uridelim,"_dists_")) == NULL){
			continue;
		}
		dist += strlen("_dists_");
		if((distdelim = strchr(dist,'_')) == NULL){
			continue;
		}
		for(suffix = suffixes ; *suffix ; ++suffix){
			if(strlen(dent.d_name) < strlen(*suffix)){
				continue;
			}
			if(strcmp(dent.d_name + strlen(dent.d_name) - strlen(*suffix),*suffix) == 0){
				int err;

				if((pl = parse_packages_file_internal(dent.d_name,&err,0)) == NULL){
					return NULL;
				}
				if((pl->distribution = strndup(dist,distdelim - dist)) == NULL){
					free_package_list(pl);
					return NULL;
				}
				if((pl->uri = strndup(dent.d_name,uridelim - dist)) == NULL){
					free_package_list(pl);
					return NULL;
				}
				pthread_mutex_lock(&dp->lock);
					pl->next = dp->sharedpcache->lists;
					dp->sharedpcache->lists = pl;
				pthread_mutex_unlock(&dp->lock);
				break;
			}
		}
	}
	// *err = errno;
	return NULL;
}

// len is the true length, less than or equal to the mapped length.
static int
parse_listdir(pkgcache *pc,DIR *dir,int *err){
	struct dirparse dp = {
		.dir = dir,
		.sharedpcache = pc,
	};
	blossom_ctl bctl = {
		.flags = 0,
		.tids = 1,
	};
	blossom_state bs;
	int r;

	if( (r = pthread_mutex_init(&dp.lock,NULL)) ){
		*err = r;
		return -1;
	}
	if(blossom_per_pe(&bctl,&bs,NULL,parse_dir,&dp)){
		*err = errno;
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	if(blossom_validate_joinrets(&bs)){
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	blossom_free_state(&bs);
	if((r = pthread_mutex_destroy(&dp.lock))){
		*err = r;
		return -1;
	}
	return 0;
}

PUBLIC pkgcache *
parse_packages_dir(const char *dir,int *err){
	pkgcache *pc;
	DIR *d;

	if((pc = create_pkgcache(NULL,err)) == NULL){
		return NULL;
	}
	if((d = opendir(dir)) == NULL){
		*err = errno;
		free_package_cache(pc);
		return NULL;
	}
	// Change directory so that relative dent.d_name entries can be opened
	if(chdir(dir)){
		*err = errno;
		closedir(d);
		free_package_cache(pc);
		return NULL;
	}
	if(parse_listdir(pc,d,err)){
		closedir(d);
		free_package_cache(pc);
		return NULL;
	}
	if(closedir(d)){
		free_package_cache(pc);
		return NULL;
	}
	return pc;
}

PUBLIC const pkgobj *
pkglist_find(const pkglist *pl,const char *pkg){
	assert(pl && pkg); // FIXME
	return NULL;
}
