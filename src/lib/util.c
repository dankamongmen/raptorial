#include <util.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

size_t maplen(size_t len){
	size_t mlen;
	int pg;

	// Probably ought get the largest page size; this will be the smallest
	// on all platforms of which I'm aware. FIXME
	if((pg = sysconf(_SC_PAGE_SIZE)) <= 0){
		return -1;
	}
	mlen = len;
	if(mlen % pg != mlen / pg){
		mlen = (mlen / pg) * pg + pg;
	}
	return mlen;
}

void *mapit(const char *path,size_t *len,int *iofd,int huge,int *err){
	struct stat st;
	size_t mlen;
	void *map;
	int fd;

	if((fd = open(path,O_CLOEXEC)) < 0){
		*err = errno;
		return MAP_FAILED;
	}
	if(fstat(fd,&st)){
		*err = errno;
		close(fd);
		return NULL;
	}
	if((mlen = maplen(st.st_size)) == (size_t)-1){
		*err = errno;
		close(fd);
		return NULL;
	}
	if(huge){
		map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_POPULATE|MAP_HUGETLB,fd,0);
	}else{
		map = MAP_FAILED;
	}
	if(map == MAP_FAILED){
		if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_POPULATE,fd,0)) == MAP_FAILED){
			*err = errno;
			close(fd);
			return MAP_FAILED;
		}
	}
	*len = st.st_size;
	*iofd = fd;
	return map;
}
