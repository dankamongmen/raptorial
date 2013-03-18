#ifndef LIBRAPTORIAL_AAC
#define LIBRAPTORIAL_AAC

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <raptorial.h>

struct dfa;
struct dfavtx;

static inline void
free_dfactx(dfactx *dctx){
	free(dctx);
}

void *match_dfactx_char(dfactx *,int);
void *match_dfactx_string(dfactx *,const char *);

#ifdef __cplusplus
}
#endif

#endif
