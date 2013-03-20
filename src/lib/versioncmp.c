#include <ctype.h>
#include <raptorial.h>

static inline int
debccmp(int d1,int d2){
	if(d1 == '~'){
		return -1;
	}else if(d2 == '~'){
		return 1;
	}else if(isalpha(d1) && !isalpha(d2)){
		return -1;
	}else if(isalpha(d2) && !isalpha(d1)){
		return 1;
	}
	return d1 - d2;
}


// See deb-version(5) for the full details of comparing Debian version strings
int debcmp(const char *d1,const char *d2){
	while(*d1 || *d2){
		int r = debccmp(*d1,*d2);

		if(r){
			return r;
		}
	}
	return 0;
}
