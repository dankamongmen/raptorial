#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <raptorial.h>

typedef struct pkgcache {
	const void *map;
	size_t mlen;
	int fd;
} pkgcache;

static inline pkgcache *
create_pkgcache(int fd,const void *mem,size_t len,int *err){
	pkgcache *pc;

	if((pc = malloc(sizeof(*pc))) == NULL){
		*err = errno;
	}else{
		pc->map = mem;
		pc->mlen = len;
		pc->fd = fd;
	}
	return pc;
}

PUBLIC pkgcache *
parse_packages_file(const char *path,int *err){
	const void *map;
	struct stat st;
	pkgcache *pc;
	size_t mlen;
	int fd;

	if(path == NULL){
		*err = EINVAL;
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
	if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_HUGETLB|MAP_POPULATE,fd,0)) == MAP_FAILED){
		*err = errno;
		close(fd);
		return NULL;
	}
	if((pc = create_pkgcache(fd,map,mlen,err)) == NULL){
		close(fd);
		return NULL;
	}
	return pc;
}

PUBLIC pkgcache *
parse_packages_mem(const void *mem,size_t len,int *err){
	pkgcache *pc;

	if(mem == NULL || len == 0){
		*err = EINVAL;
		return NULL;
	}
	if((pc = create_pkgcache(-1,mem,len,err)) == NULL){
		return NULL;
	}
	return pc;
}

PUBLIC void
free_package_cache(pkgcache *pc){
	if(pc){
		if(pc->fd >= 0){
			close(pc->fd);
		}
		free(pc);
	}
}
