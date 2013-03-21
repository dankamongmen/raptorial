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
// FIXME doesn't handle numeric comparison part correctly:
/*
       First the initial part of each string consisting entirely of  non-digit
       characters  is determined.  These two parts (one of which may be empty)
       are compared lexically.  If a difference is found it is returned.   The
       lexical comparison is a comparison of ASCII values modified so that all
       the letters sort earlier than all the non-letters and so that  a  tilde
       sorts  before  anything, even the end of a part.  For example, the fol‐
       lowing parts are in sorted order: '~~', '~~a',  '~',  the  empty  part,
       'a'.

       Then  the  initial  part of the remainder of each string which consists
       entirely of digit characters is determined.  The  numerical  values  of
       these  two  parts are compared, and any difference found is returned as
       the result of the comparison.   For  these  purposes  an  empty  string
       (which  can  only occur at the end of one or both version strings being
       compared) counts as zero.*/

int debcmp(const char *d1,const char *d2){
	while(*d1 || *d2){
		int r = debccmp(*d1,*d2);

		if(r){
			return r;
		}
		++d1;
		++d2;
	}
	return 0;
}
