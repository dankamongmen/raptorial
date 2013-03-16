#include <aac.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <raptorial.h>

typedef struct edge {
	// Use offsets into a dynamic array rather than pointers so we needn't
	// update all references upon array reallocation. This allows up to
	// 2^32 nodes, and a signed alphabet of up to 2^32 letters.
	uint32_t vtx;
	// FIXME label really ought be a wchar_t, and we ought take input
	// multibytes to widechars. 4 bytes is not enough to store all utf8!
	int32_t label;
} edge;

typedef struct dfavtx {
	unsigned setsize; // Dynamic array of edges, sorted by edge label
	struct edge *set; // Increases by one; no need for total count
	void *val; // Match at this node
} dfavtx;

// Entrypoint of the automaton is always vtxarray[0]
typedef struct dfa {
	dfavtx *vtxarray; // Dynamic array of dfavtxs
	unsigned vtxcount,vtxalloc;
} dfa;

dfactx *create_dfactx(const dfa *space){
	dfactx *dctx;

	if( (dctx = malloc(sizeof(*dctx))) ){
		dctx->dfa = space;
		dctx->cur = space->vtxarray;
	}
	return dctx;
}

// We can't just use bsearch(3) -- see comment in augment_dfa(); we want to
// know the position where the needle *would have been* if it's not actually
// there. Caller must verify that the return value describes an actual match,
// if that's what they want to know. Otherwise, we'd need do another lg(e)
// search to insert the edge.
static inline unsigned
edge_search(const dfavtx *node,int s){
	unsigned max,min,pos;

	min = 0;
	max = node->setsize;
	do{
		if((pos = (min + max) / 2) == max){
			break;
		}
		if(node->set[pos].label == s){
			break;
		}
		if(s < node->set[pos].label){
			max = pos;
		}else{
			min = pos;
		}
	}while(pos != (min + max) / 2);
	return pos;
}

PUBLIC int
augment_dfa(dfa **space,const char *str,void *val){
	const char *s;
	dfavtx *cur;

	if(val == NULL){ // can't add a NULL -- how would you check for match?
		return -1;
	}
	if(*space == NULL){
		if((*space = malloc(sizeof(**space))) == NULL){
			return -1;
		}
		(*space)->vtxalloc = 8;
		if(((*space)->vtxarray = malloc(sizeof(*(*space)->vtxarray) * (*space)->vtxalloc)) == NULL){
			free(*space);
			return -1;
		}
		(*space)->vtxarray[0].setsize = 0;
		(*space)->vtxarray[0].set = NULL;
		(*space)->vtxcount = 1;
	}
	// For each successive character in the augmenting string, check to
	// see if there's already an edge so labelled, and follow it if so.
	// Otherwise, add the edge. We do a binary search to check for an edge,
	// and thus know precisely where we need insert the new edge to keep
	// the list sorted. This phase is thus nlg(e) (n characters X lg(e)
	// edge searches). We can't use bsearch(3) because it doesn't provide
	// the position where the value ought have been on failure.
	for(cur = (*space)->vtxarray, s = str ; *s ; ++s){
		unsigned pos;

		pos = edge_search(cur,*s);
		if(pos >= cur->setsize || cur->set[pos].label != *s){
			struct edge *tmp;

			if((*space)->vtxcount == (*space)->vtxalloc){
				dfavtx *tv;

				if((tv = realloc((*space)->vtxarray,sizeof(*(*space)->vtxarray) * (*space)->vtxalloc * 2)) == NULL){
					return -1;
				}
				cur = tv + (cur - (*space)->vtxarray);
				(*space)->vtxarray = tv;
				(*space)->vtxalloc *= 2;
			}
			if((tmp = realloc(cur->set,sizeof(*cur->set) * (cur->setsize + 1))) == NULL){
				return -1;
			}
			cur->set = tmp;
			if(pos < cur->setsize){
				memmove(cur->set + pos + 1,cur->set + pos,
					sizeof(cur->set) * (cur->setsize - pos));
			}
			++cur->setsize;
			cur->set[pos].label = *s;
			cur->set[pos].vtx = (*space)->vtxcount++;
			(*space)->vtxarray[cur->set[pos].vtx].setsize = 0;
			(*space)->vtxarray[cur->set[pos].vtx].set = NULL;
			(*space)->vtxarray[cur->set[pos].vtx].val = NULL;
		}
		cur = (*space)->vtxarray + cur->set[pos].vtx;
	}
	if(cur->val){ // Already have this pattern!
		return -1;
	}
	cur->val = val;
	return 0;
}

void free_dfa(dfa *space){
	if(space){
		free(space->vtxarray);
		free(space);
	}
}

void *match_dfactx_char(dfactx *dctx,int s){
	unsigned pos = edge_search(dctx->cur,s);

	if(pos < dctx->cur->setsize && dctx->cur->set[pos].label != s){
		// FIXME take the sigma (failure) function
		return NULL;
	}
	dctx->cur = dctx->dfa->vtxarray + dctx->cur->set[pos].vtx;
	return dctx->cur->val;
}

void *match_dfactx_string(dfactx *dctx,const char *str){
	while(*str){
		match_dfactx_char(dctx,*str);
		++str;
	}
	return dctx->cur->val;
}
