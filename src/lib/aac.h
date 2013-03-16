#ifndef LIBRAPTORIAL_AAC
#define LIBRAPTORIAL_AAC

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

struct dfa;

int augment_dfa(struct dfa **);

void free_dfa(struct dfa *);

typedef struct dfactx {
	const struct dfa *dfa,*cur;
} dfactx;

static inline dfactx *
create_dfactx(const struct dfa *dfa){
	dfactx *dctx;

	if( (dctx = malloc(sizeof(*dctx))) ){
		dctx->dfa = dfa;
		dctx->cur = dfa;
	}
	return dctx;
}

static inline void
free_dfactx(dfactx *dctx){
	free(dctx);
}

void match_dfactx_char(dfactx *,int);
void match_dfactx_string(dfactx *,const char *);

#ifdef __cplusplus
}
#endif

#endif
