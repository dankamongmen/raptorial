#include <errno.h>
#include <pthread.h>
#include <raptorial.h>

typedef struct pkgcache {
} pkgcache;

PUBLIC pkgcache *
parse_packages_file(const char *path,int *err){
	if(path == NULL){
		*err = EINVAL;
		return NULL;
	}
	return NULL;
}

PUBLIC pkgcache *
parse_packages_mem(const void *mem,size_t len,int *err){
	if(mem == NULL || len == 0){
		*err = EINVAL;
		return NULL;
	}
	return NULL;
}
