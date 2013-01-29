#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
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
} pkgcache;

struct pkgparse {
	unsigned count;
	const void *mem;
	size_t len,csize;
};

// Threads handle chunks of the packages list. Since we don't know where 
// package definitions are split in the list data, we'll usually toss some data
// from the front (it was handled as part of another chunk), and grab some
// extra data from the back. These will typically be small amounts, and the
// data is read-only, so rather than track splits (requiring synchronization), 
// we just always locally discover the bounds (put another way, the overlapping
// areas at the front and back of chunks are lexed twice).
typedef struct pkgchunk {
	size_t offset;
	const struct pkgparse *pp;
} pkgchunk;

static void *
parse_chunk(void *vpc){
	const char *start,*c,*end;
	const pkgchunk *pc = vpc;

	start = (const char *)pc->pp->mem + pc->offset;
	if(pc->pp->csize + pc->offset > pc->pp->len){
		end = start + (pc->pp->len - pc->offset);
	}else{
		end = start + pc->pp->csize;
	}
	for(c = start ; c < end ; ++c){
		// FIXME
	}
	return vpc;
}

// len is the true length, less than or equal to the mapped length.
static int
parse_map(const void *mem,size_t len,int *err){
	struct pkgparse pp = {
		.mem = mem,
		.len = len,
	};
	blossom_ctl bctl = {
		.flags = 0,
		.tids = 1,
	};
	blossom_state bs;

	if(blossom_per_pe(&bctl,&bs,NULL,parse_chunk,&pp)){
		*err = errno;
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		return -1;
	}
	blossom_free_state(&bs);
	return 0;
}

static inline pkgcache *
create_pkgcache(const void *mem,size_t len,int *err){
	pkgcache *pc;

	if((pc = malloc(sizeof(*pc))) == NULL){
		*err = errno;
	}else if(parse_map(mem,len,err)){
		free(pc);
		pc = NULL;
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
