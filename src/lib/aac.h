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

void *match_dfactx_string(dfactx *,const char *);
void *match_dfactx_nstring(dfactx *,const char *,size_t);
void *match_dfactx_against_nstring(dfactx *,const char *,size_t);

#ifdef __cplusplus
}
#endif

#endif
