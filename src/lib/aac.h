#ifndef LIBRAPTORIAL_AAC
#define LIBRAPTORIAL_AAC

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

struct dfa;
struct dfavtx;

typedef struct dfactx {
	const struct dfa *dfa;
	const struct dfavtx *cur;
} dfactx;

dfactx *create_dfactx(const struct dfa *);

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
