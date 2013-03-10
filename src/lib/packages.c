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

typedef struct pkgobj {
	struct pkgobj *next;
	char *name;
	char *version;
} pkgobj;

typedef struct pkgcache {
	pkgobj *pobjs;
	unsigned pcount;
} pkgcache;

struct pkgparse {
	unsigned count;
	const void *mem;
	size_t len,csize;
	pthread_mutex_t lock;

	// These data are modified by threads, and must be protected by the
	// lock. A thread takes the chunk at offset, incrementing offset by
	// the chunksize. Parsed pkgobjs are placed in sharedpcache.
	size_t offset;
	pkgcache *sharedpcache;
};

static void
free_package(pkgobj *po){
	free(po->version);
	free(po->name);
	free(po);
}

static pkgobj *
create_package(const char *name,size_t namelen,const char *ver,size_t verlen){
	pkgobj *po;

	if( (po = malloc(sizeof(*po))) ){
		if((po->version = malloc(sizeof(*po->version) * (verlen + 1))) == NULL){
			free(po);
			return NULL;
		}
		if((po->name = malloc(sizeof(*po->name) * (namelen + 1))) == NULL){
			free(po->version);
			free(po);
			return NULL;
		}
		strncpy(po->version,ver,verlen);
		po->version[verlen] = '\0';
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
	STATE_V,
	STATE_VE,
	STATE_VER,
	STATE_VERS,
	STATE_VERSI,
	STATE_VERSIO,
	STATE_VERSION,
	STATE_VERSION_DELIM,
	STATE_VERSION_DELIMITED,
	STATE_P,
	STATE_PA,
	STATE_PAC,
	STATE_PACK,
	STATE_PACKA,
	STATE_PACKAG,
	STATE_PACKAGE,
	STATE_PACKAGE_DELIM,
	STATE_PACKAGE_DELIMITED,
};

// Threads handle chunks of the packages list. Since we don't know where
// package definitions are split in the list data, we'll usually toss some data
// from the front (it was handled as part of another chunk), and grab some
// extra data from the back. These will typically be small amounts, and the
// data is read-only, so rather than track splits (requiring synchronization),
// we just always locally discover the bounds (put another way, the overlapping
// areas at the front and back of chunks are lexed twice).
//
// We're not treating the input as anything but ASCII text, though it's almost
// certainly UTF8. Need to ensure that newlines and pattern tags are all
// strictly ASCII or change how we handle things FIXME.
static void *
parse_chunk(void *vpp){
	const char *start,*c,*end,*delim,*pname,*pver,*veryend;
	struct pkgparse *pp = vpp;
	size_t offset = 0; // FIXME
	unsigned packages = 0;
	pkgobj *head,*po,**enq;
	size_t pnamelen,pverlen;
	int state;

	head = NULL;
	offset = get_new_offset(pp);
	// We can go past the end of our chunk to finish a package's parsing
	// in media res, but we can't go past the end of the actual map!
	veryend = (const char *)pp->mem + pp->len;
	while(offset < pp->len){
		start = (const char *)pp->mem + offset;
		if(pp->csize + offset > pp->len){
			end = start + (pp->len - offset);
		}else{
			end = start + pp->csize;
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
			assert(pp->csize > 2); // sanity check
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

		enq = &head;
		// We are at the beginning of our chunk, which might be 0 bytes. Any
		// partial record with which our map started has been skipped
		state = STATE_RESET; // number of newlines we've seen, bounded by 2
		// Upon reaching the (optional, only one allowed) delimiter on each
		// line, delim will be updated to point one past that delimiter (which
		// might be outside the chunk!), and to chew whitespace.
		delim = NULL;
		// We hand create_package our raw map bytes; it allocates the destbuf.
		// These are thus reset on each package.
		pname = NULL; pver = NULL;
		pnamelen = 0; pverlen = 0;
		while(c < end || (state != STATE_RESET && c < veryend)){
			if(*c == '\n'){ // State machine is driven by newlines
				if(state == STATE_NLINE){ // double newline
					if(pname == NULL || pnamelen == 0){
						goto err; // No package name
					}
					if(pver == NULL || pverlen == 0){
						goto err; // No package version
					}
					if((po = create_package(pname,pnamelen,pver,pverlen)) == NULL){
						goto err;
					}
					// Package ended!
					++packages;
					*enq = po;
					enq = &po->next;
					pname = NULL;
					pver = NULL;
					state = STATE_RESET;
				}else{ // We processed a line of the current package
					if(state == STATE_PACKAGE_DELIMITED){
	// Don't allow a package to be named twice. Defined another way, require
	// an empty line between every two instances of a Package: line.
						if(pname){
							goto err;
						}
						pnamelen = c - delim;
						pname = delim;
					}else if(state == STATE_VERSION_DELIMITED){
						if(pver){
							goto err;
						}
						pverlen = c - delim;
						pver = delim;
					}
					state = STATE_NLINE;
				}
			}else switch(state){ // not a newline
			case STATE_NLINE:
			case STATE_RESET:
				delim = NULL;
				start = c;
				if(*c == 'V'){
					state = STATE_V;
				}else if(*c == 'P'){
					state = STATE_P;
				}else{
					state = STATE_PDATA;
				}
				break;
			case STATE_V:
				state = *c == 'e' ? STATE_VE : STATE_PDATA;
				break;
			case STATE_VE:
				state = *c == 'r' ? STATE_VER : STATE_PDATA;
				break;
			case STATE_VER:
				state = *c == 's' ? STATE_VERS : STATE_PDATA;
				break;
			case STATE_VERS:
				state = *c == 'i' ? STATE_VERSI : STATE_PDATA;
				break;
			case STATE_VERSI:
				state = *c == 'o' ? STATE_VERSIO : STATE_PDATA;
				break;
			case STATE_VERSIO:
				state = *c == 'n' ? STATE_VERSION : STATE_PDATA;
				break;
			case STATE_VERSION:
				state = *c == ':' ? STATE_VERSION_DELIM : STATE_PDATA;
				delim = c + 1;
				break;
			case STATE_VERSION_DELIM:
				if(isspace(*c)){
					++delim;
				}else{
					state = STATE_VERSION_DELIMITED;
				}
				break;
			case STATE_P:
				state = *c == 'a' ? STATE_PA : STATE_PDATA;
				break;
			case STATE_PA:
				state = *c == 'c' ? STATE_PAC : STATE_PDATA;
				break;
			case STATE_PAC:
				state = *c == 'k' ? STATE_PACK : STATE_PDATA;
				break;
			case STATE_PACK:
				state = *c == 'a' ? STATE_PACKA : STATE_PDATA;
				break;
			case STATE_PACKA:
				state = *c == 'g' ? STATE_PACKAG : STATE_PDATA;
				break;
			case STATE_PACKAG:
				state = *c == 'e' ? STATE_PACKAGE : STATE_PDATA;
				break;
			case STATE_PACKAGE:
				state = *c == ':' ? STATE_PACKAGE_DELIM : STATE_PDATA;
				delim = c + 1;
				break;
			case STATE_PACKAGE_DELIM:
				if(isspace(*c)){
					++delim;
				}else{
					state = STATE_PACKAGE_DELIMITED;
				}
				break;
			}
			++c;
		}
		if(state < 2){
			goto err; // map ended in the middle of a package
		}
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
	assert(0); // FIXME remove once return values are checked in pthread_join
	while( (po = head) ){
		head = po->next;
		free_package(po);
	}
	return NULL;
}

// len is the true length, less than or equal to the mapped length.
static int
parse_map(pkgcache *pc,const void *mem,size_t len,int *err){
	struct pkgparse pp = {
		.mem = mem,
		.len = len,
		.offset = 0,
		.sharedpcache = pc,
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
	if(blossom_per_pe(&bctl,&bs,NULL,parse_chunk,&pp)){
		*err = errno;
		pthread_mutex_destroy(&pp.lock);
		return -1;
	}
	// FIXME need check all return values and ensure they're non-NULL
	if(blossom_join_all(&bs)){
		*err = errno;
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

static inline pkgcache *
create_pkgcache(const void *mem,size_t len,int *err){
	pkgcache *pc;

	if((pc = malloc(sizeof(*pc))) == NULL){
		*err = errno;
	}else{
		memset(pc,0,sizeof(*pc));
		if(parse_map(pc,mem,len,err)){
			free(pc);
			return NULL;
		}
	}
	return pc;
}

PUBLIC pkgcache *
parse_packages_file(const char *path,int *err){
	const void *map;
	size_t mlen,len;
	struct stat st;
	pkgcache *pc;
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
	if((pc = create_pkgcache(map,len,err)) == NULL){
		close(fd);
		return NULL;
	}
	close(fd);
	return pc;
}

PUBLIC pkgcache *
parse_packages_dir(const char *dir,int *err){
	struct dirent dent,*pdent;
	pkgcache *pc = NULL;
	DIR *d;

	if((d = opendir(dir)) == NULL){
		*err = errno;
		return NULL;
	}
	// Change directory so that relative dent.d_name entries can be opened
	if(chdir(dir)){
		*err = errno;
		closedir(d);
		return NULL;
	}
	while(readdir_r(d,&dent,&pdent) == 0){
		const char *suffixes[] = { "Sources", "Packages", NULL },**suffix;

		if(pdent == NULL){
			if(closedir(d)){
				*err = errno;
				free_package_cache(pc);
				return NULL;
			}
			return pc;
		}
		if(dent.d_type != DT_REG && dent.d_type != DT_LNK){
			continue; // FIXME maybe don't skip DT_UNKNOWN?
		}
		for(suffix = suffixes ; *suffix ; ++suffix){
			if(strlen(dent.d_name) < strlen(*suffix)){
				continue;
			}
			if(strcmp(dent.d_name + strlen(dent.d_name) - strlen(*suffix),*suffix) == 0){
				// FIXME want a non-destructive union operation. this throws
				// away (leaks) results thus far to get new ones.
				if((pc = parse_packages_file(dent.d_name,err)) == NULL){
					closedir(d);
					free_package_cache(pc);
					return NULL;
				}
				break;
			}
		}
	}
	*err = errno;
	free_package_cache(pc);
	closedir(d);
	return pc;
}

PUBLIC pkgcache *
parse_packages_mem(const void *mem,size_t len,int *err){
	pkgcache *pc;

	if(mem == NULL || len == 0){
		*err = EINVAL;
		return NULL;
	}
	if((pc = create_pkgcache(mem,len,err)) == NULL){
		return NULL;
	}
	return pc;
}

PUBLIC void
free_package_cache(pkgcache *pc){
	if(pc){
		free(pc);
	}
}

PUBLIC pkgobj *
pkgcache_begin(pkgcache *pc){
	return pc->pobjs;
}

PUBLIC pkgobj *
pkgcache_next(pkgobj *po){
	return po->next;
}

PUBLIC const pkgobj *
pkgcache_cbegin(const pkgcache *pc){
	return pc->pobjs;
}

PUBLIC const pkgobj *
pkgcache_cnext(const pkgobj *po){
	return po->next;
}

PUBLIC const char *
pkgcache_name(const pkgobj *po){
	return po->name;
}

PUBLIC const char *
pkgcache_version(const pkgobj *po){
	return po->version;
}

PUBLIC unsigned
pkgcache_count(const pkgcache *po){
	return po->pcount;
}
