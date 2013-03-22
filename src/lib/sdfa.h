#ifndef LIBRAPTORIAL_SDFA
#define LIBRAPTORIAL_SDFA

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <raptorial.h>

struct sdfa;
struct sdfavtx;

static inline void
free_sdfactx(sdfactx *dctx){
	free(dctx);
}

void *match_sdfactx_char(sdfactx *,int);
void *match_sdfactx_string(sdfactx *,const char *);
void *match_sdfactx_nstring(sdfactx *,const char *,size_t);

#ifdef __cplusplus
}
#endif

#endif
