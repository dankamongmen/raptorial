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

static void *
parse_chunk(void *pc){
	return pc; // FIXME
}

struct pkgparse {
	unsigned count;
	const void *mem;
	size_t len;
};

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
	struct stat st;
	pkgcache *pc;
	size_t mlen;
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
	mlen = st.st_size;
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
	if((pc = create_pkgcache(map,mlen,err)) == NULL){
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
