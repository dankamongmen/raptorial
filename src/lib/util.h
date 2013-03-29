#ifndef RAPTORIAL_UTIL
#define RAPTORIAL_UTIL

// private utility functions for raptorial
#include <ctype.h>
#include <stddef.h>

size_t maplen(size_t);
void *mapit(const char *,size_t *,int *,int,int *);

static inline int
isdebpkgchar(int c){
	return isalnum(c) || c == '-';
}

static inline int
isdebverchar(int c){
	return isalnum(c) || c == '-' || c == '+' || c == '.' || c == ':' || c == '~';
}

#endif
