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
void *match_dfactx_nstring(dfactx *,const char *,size_t);

// Take the union of src and dst into the return value. Neither src nor dst
// are guaranteed usable any longer, unless they're incidently returned.
struct dfa *combine_dfas(struct dfa *,struct dfa *);

#ifdef __cplusplus
}
#endif

#endif
