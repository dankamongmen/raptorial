#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <raptorial.h>

typedef struct pkgcache {
} pkgcache;

static inline pkgcache *
create_pkgcache(int *err){
	pkgcache *pc;

	if((pc = malloc(sizeof(*pc))) == NULL){
		*err = errno;
	}
	return pc;
}

PUBLIC pkgcache *
parse_packages_file(const char *path,int *err){
	pkgcache *pc;

	if(path == NULL){
		*err = EINVAL;
		return NULL;
	}
	if((pc = create_pkgcache(err)) == NULL){
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
	if((pc = create_pkgcache(err)) == NULL){
		return NULL;
	}
	return pc;
}

PUBLIC void
free_package_cache(pkgcache *pc){
	free(pc);
}
