#include <util.h>
#include <unistd.h>

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
