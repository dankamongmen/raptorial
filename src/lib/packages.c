#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
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
	const char *start,*c,*end,*delim,*pname,*pver;
	struct pkgparse *pp = vpp;
	size_t offset = 0; // FIXME
	unsigned packages = 0;
	pkgobj *head,*po,**enq;
	size_t pnamelen,pverlen;
	int state;

	head = NULL;
	offset = get_new_offset(pp);
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
			state = 0;
			assert(pp->csize > 2); // sanity check
			for(c = start - 2 ; c < end ; ++c){
				if(*c == '\n'){
					if(state){
						++c;
						break;
					}
					state = 1;
				}else{
					state = 0;
				}
			}
		}else{
			c = start;
		}

		enq = &head;
		// We are at the beginning of our chunk, which might be 0 bytes. Any
		// partial record with which our map started has been skipped
		state = 2; // number of newlines we've seen, bounded by 2
		// Upon reaching the (optional, only one allowed) delimiter on each
		// line, delim will be updated to point one past that delimiter (which
		// might be outside the chunk!), and to chew whitespace.
		delim = NULL;
		// We hand create_package our raw map bytes; it allocates the destbuf.
		// These are thus reset on each package.
		pname = NULL; pver = NULL;
		pnamelen = 0; pverlen = 0;
		while(c < end){
			if(*c == '\n'){ // State machine is driven by newlines
				if(++state == 2){
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
				}else{ // We processed a line of the current package
					if(delim){ // Was line delimited?
						if((size_t)(c - start) >= strlen("Package:")){
							if(strncmp(start,"Package:",strlen("Package:")) == 0){
	// Don't allow a package to be named twice. Defined another way, require
	// an empty line between every two instances of a Package: line.
								if(pname){
									goto err;
								}
								pnamelen = c - delim;
								pname = delim;
							}else if(strncmp(start,"Version:",strlen("Version:")) == 0){
								if(pver){
									goto err;
								}
								pverlen = c - delim;
								pver = delim;
							}
						}
					}
				}
			}else{ // not a newline
				if(state){
					delim = NULL;
					start = c;
					state = 0;
				}
				if(isspace(*c)){
					if(c == delim){
						++delim;
					}
				}else if(*c == ':'){
					delim = c + 1;
				}
			}
			++c;
		}
		offset = get_new_offset(pp);
	}
	// FIXME if we're in the middle of a record, we read into the next map
	// (hence why we skipped one if we started in media res)
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
