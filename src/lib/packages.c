#include <aac.h>
#include <util.h>
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
#include <sys/types.h>
#include <raptorial.h>

// For now, the datastore is a trie, anchored by selection packages (either
// those specified on the command line, or those currently installed). These
// dist-wide linked lists ought not be used for searching, and no interface is
// provided to do so.
typedef struct pkgobj {
	struct pkgobj *next;
	char *name;
	char *version;
	const struct pkglist *pl;
	int haslock;

	pthread_mutex_t lock;
	struct pkgobj *dfanext;
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
	struct dfa **dfa;
	pthread_mutex_t lock;

	// These data are modified by threads, and must be protected by the
	// lock. A thread takes the chunk at offset, incrementing offset by
	// the chunksize. Parsed pkgobjs are placed in sharedpcache.
	size_t offset;
	pkglist *sharedpcache;
};

static void
free_package(pkgobj *po){
	if(po->haslock){
		assert(pthread_mutex_destroy(&po->lock) == 0);
	}
	free(po->version);
	free(po->name);
	free(po);
}

static inline int
fill_package(pkgobj *po,const char *ver,size_t verlen,const pkglist *pl){
	if(ver){
		if((po->version = malloc(sizeof(*po->version) * (verlen + 1))) == NULL){
			return -1;
		}
		strncpy(po->version,ver,verlen);
		po->version[verlen] = '\0';
	}else{
		po->version = NULL;
	}
	po->dfanext = NULL;
	po->next = NULL;
	po->pl = pl;
	return 0;
}

static pkgobj *
create_package(const char *name,size_t namelen,const char *ver,size_t verlen,
					const pkglist *pl){
	pkgobj *po;

	if( (po = malloc(sizeof(*po))) ){
		if((po->name = malloc(sizeof(*po->name) * (namelen + 1))) == NULL){
			free(po);
			return NULL;
		}
		strncpy(po->name,name,namelen);
		po->name[namelen] = '\0';
		if(fill_package(po,ver,verlen,pl)){
			free(po->name);
			free(po);
		}
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
//
// Typical rules for DFA: If NULL, it's unused. If it points to a pointer to
// NULL, construct it from whatever we find. If it points to a non-NULL
// pointer, use that as a filter for construction of our list.
static int
lex_chunk(size_t offset,const char *start,const char *end,
		const char *veryend,pkgobj ***enq,
		struct pkgparse *pp){
	const char *expect,*pname,*pver,*pstatus,*c,*delim;
	size_t pnamelen,pverlen;
	int rewardstate,state;
	unsigned newp = 0;
	unsigned filter;
	dfactx dctx;
	pkgobj *po;

	// filter is 0 if we're building the dfa, 1 if we're being filtered by
	// the dfa, and undefined if dga == NULL
	if(pp->dfa){
		filter = *(pp->dfa) ? 1 : 0;
	}else{
		filter = 0; // FIXME get rid of this when gcc improves
	}
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
	pverlen = pnamelen = 0;
	while(c < end || (state != STATE_RESET && c < veryend)){
		if(*c == '\n'){ // State machine is driven by newlines
			switch(state){
			case STATE_NLINE:{ // double newline
				if(pname == NULL || pnamelen == 0){
					return -1; // No package name
				}
				if(pver == NULL || pverlen == 0){
					if(!pp->statusfile){
						return -1; // No package version
					}
				}
				if(!pp->statusfile || pstatus){
					if(pp->dfa && filter){
						pkgobj *mpo;

						init_dfactx(&dctx,*pp->dfa);
						if( (mpo = match_dfactx_nstring(&dctx,pname,pnamelen)) ){
							if((po = create_package(pname,pnamelen,pver,pverlen,pp->sharedpcache)) == NULL){
								return -1;
							}
							pthread_mutex_lock(&mpo->lock);
							po->dfanext = mpo->dfanext;
							mpo->dfanext = po;
							pthread_mutex_unlock(&mpo->lock);
							po->haslock = 0;
						}else{
							po = NULL;
						}
					}else if((po = create_package(pname,pnamelen,pver,pverlen,pp->sharedpcache)) == NULL){
						return -1;
					}else if(pthread_mutex_init(&po->lock,NULL)){
						free_package(po);
					}else{
						po->haslock = 1;
					}
				}else{
					po = NULL;
				}
				// Package ended!
				if(po){
					++newp;
					**enq = po;
					*enq = &po->next;
				}
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
				break;
			case STATE_VERSION_DELIMITED:
				if(pver){
					return -1;
				}
				pverlen = c - delim;
				pver = delim;
				break;
			case STATE_STATUS_DELIMITED:
				if(pstatus){
					return -1;
				}
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
					expect = "tatus: install ";
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
lex_chunks(void *vpp){
	const char *start,*end,*veryend;
	struct pkgparse *pp = vpp;
	pkgobj *head,**enq,*po;
	unsigned packages = 0;
	unsigned filter;
	size_t offset;

	head = NULL;
	enq = &head;
	filter = pp->dfa && *(pp->dfa) ? 1 : 0;
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
		newp = lex_chunk(offset,start,end,veryend,&enq,pp);
		if(newp < 0){
			goto err;
		}
		packages += newp;
		offset = get_new_offset(pp);
	}
	if(head){
		pthread_mutex_lock(&pp->lock); // Success!
			if(pp->dfa && !filter){
				for(po = head ; po ; po = po->next){
					augment_dfa(pp->dfa,po->name,po);
				}
			}
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
	struct dfa *dfa;
	pkgcache *sharedpcache;
	pthread_mutex_t lock;
};

static inline pkglist *
create_pkglist(const void *mem,size_t len,int *err,int statusfile,struct dfa **dfa){
	pkglist *pl;

	if((pl = malloc(sizeof(*pl))) == NULL){
		*err = errno;
	}else{
		struct pkgparse pp = {
			.statusfile = statusfile,
			.dfa = dfa,
			.mem = mem,
			.len = len,
			.offset = 0,
			.sharedpcache = pl,
			.csize = 1024 * 1024,
		};

		memset(pl,0,sizeof(*pl));
		if(lex_chunks(&pp) == NULL){
			// FIXME set *err
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
lex_packages_file_internal(const char *path,int *err,int statusfile,
						struct dfa **dfa){
	const void *map;
	size_t mlen;
	pkglist *pl;
	int fd;

	if(path == NULL){
		*err = EINVAL;
		return NULL;
	}
	if((map = mapit(path,&mlen,&fd,1,err)) == MAP_FAILED){
		return NULL;
	}
	if((pl = create_pkglist(map,mlen,err,statusfile,dfa)) == NULL){
		close(fd);
		return NULL;
	}
	close(fd);
	return pl;
}

PUBLIC pkglist *
lex_packages_file(const char *path,int *err,struct dfa **dfa){
	return lex_packages_file_internal(path,err,0,dfa);
}

PUBLIC pkglist *
lex_packages_mem(const void *mem,size_t len,int *err,struct dfa **dfa){
	pkglist *pl;

	if(mem == NULL || len == 0){
		*err = EINVAL;
		return NULL;
	}
	if((pl = create_pkglist(mem,len,err,0,dfa)) == NULL){
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
lex_status_file(const char *path,int *err,struct dfa **dfa){
	return lex_packages_file_internal(path,err,1,dfa);
}

PUBLIC const pkglist *
pkgcache_begin(const pkgcache *pc){
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
lex_dir(void *vdp){
	struct dirparse *dp = vdp;
	struct dirent dent,*pdent;
	struct dfa **dfap;
	int r;
       
	dfap = dp->dfa ? &dp->dfa : NULL;
	while( (r = readdir_r(dp->dir,&dent,&pdent)) == 0 && pdent){
		const char *suffixes[] = { "Sources", "Packages", NULL },**suffix;
		const char *distdelim,*dist,*uridelim;
		pkglist *pl;

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

				if((pl = lex_packages_file_internal(dent.d_name,&err,0,dfap)) == NULL){
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
	if(r){
		return NULL;
	}
	return dp;
}

// len is the true length, less than or equal to the mapped length.
static int
lex_listdir(pkgcache *pc,DIR *dir,int *err,struct dfa *dfa){
	struct dirparse dp = {
		.dir = dir,
		.dfa = dfa,
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
	if(blossom_per_pe(&bctl,&bs,NULL,lex_dir,&dp)){
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
lex_packages_dir(const char *dir,int *err,struct dfa *dfa){
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
	if(lex_listdir(pc,d,err,dfa)){
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

PUBLIC const struct pkgobj *
pkgcache_find_installed(const pkgobj *mpo){
	const pkgobj *po;

	if(mpo->version == NULL){
		return NULL;
	}
	for(po = mpo->dfanext ; po ; po = po->dfanext){
		if(po->version && debcmp(po->version,mpo->version) == 0){
			return po;
		}
	}
	return po;
}

PUBLIC const struct pkgobj *
pkgcache_find_newest(const pkgobj *mpo){
	const pkgobj *po,*newest = NULL;

	for(po = mpo->dfanext ; po ; po = po->dfanext){
		if(!newest || debcmp(newest->version,po->version) < 0){
			newest = po;
		}
	}
	return newest;
}

struct pkgobj *create_stub_package(const char *name,int *err){
	pkgobj *po;
	int r;

	if((po = create_package(name,strlen(name),NULL,0,NULL)) == NULL){
		*err = errno;
	}else if( (r = pthread_mutex_init(&po->lock,NULL)) ){
		*err = r;
		free(po);
		po = NULL;
	}else{
		po->haslock = 1;
	}
	return po;
}

const pkgobj *
pkgobj_matchbegin(const pkgobj *mpo){
	return mpo->dfanext;
}

const pkgobj *
pkgobj_matchnext(const pkgobj *po){
	return po->dfanext;
}

const char *
pkgobj_uri(const pkgobj *po){
	return pkglist_uri(po->pl);
}

PUBLIC const char *
pkgobj_dist(const pkgobj *po){
	return pkglist_dist(po->pl);
}
