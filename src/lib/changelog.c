#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <raptorial.h>

typedef struct changelog {
} changelog;

static void
free_changelog(changelog *cl){
	free(cl);
}

changelog *lex_changelog(const char *fn,int *err){
	struct stat st;
	changelog *cl;
	int fd;

	if((fd = open(fn,O_CLOEXEC)) < 0){
		*err = errno;
		return NULL;
	}
	if(fstat(fd,&st)){
		*err = errno;
		close(fd);
		return NULL;
	}
	if((cl = malloc(sizeof(*cl))) == NULL){
		*err = errno;
		close(fd);
		return NULL;
	}
	if(close(fd)){
		*err = errno;
		free_changelog(cl);
		close(fd);
		return NULL;
	}
	return cl;
}
