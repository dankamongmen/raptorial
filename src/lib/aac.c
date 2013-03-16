#include <aac.h>
#include <stdint.h>

typedef struct edge {
	// Use offsets into a dynamic array rather than pointers so we needn't
	// update all references upon array reallocation. This allows up to
	// 2^32 nodes, and an alphabet of up to 2^32 letters.
	//
	// FIXME label really ought be a wchar_t, and we ought take input
	// multibytes to widechars. 4 bytes is not enough to store all utf8!
	uint32_t label,vtx;
} edge;

typedef struct dfavtx {
	// Dynamic array of edges, sorted by edge label (for binary searching)
	unsigned setsize;
	struct edge **set;
	void *val;
} dfavtx;

typedef struct dfa {
	dfavtx *entry;

	// Dynamic array of dfavtxs.
	dfavtx **vtxarray;
	unsigned vtxcount,vtxalloc;
} dfa;

dfactx *create_dfactx(const dfa *space){
	dfactx *dctx;

	if( (dctx = malloc(sizeof(*dctx))) ){
		dctx->dfa = space;
		dctx->cur = space->entry;
	}
	return dctx;
}

void free_dfa(dfa *space){
	if(space){
		free(space->vtxarray);
		free(space);
	}
}
