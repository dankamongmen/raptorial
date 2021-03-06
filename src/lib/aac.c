#include <aac.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <raptorial.h>

// Do not store pointers to either of these types (edges or dfactx's), as they
// are both stored in dynamic (moveable) arrays.
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

typedef struct bmgstate {
	unsigned char *match;	// Sole pattern when patcount == 1
	void *val;		// Value for bmgmatch pattern
	size_t len;		// Length of pattern
	int delta1[1u << (CHAR_BIT - 1)];
	int *delta2;
} bmgstate;

// Entrypoint of the automaton is always vtxarray[0]
typedef struct dfa {
	dfavtx *vtxarray;	// Dynamic array of dfavtxs
	unsigned vtxcount,vtxalloc;
	unsigned patcount;	// Number of patterns in the dfa
	ptrdiff_t longest;	// Longest pattern in the dfa + 1
	bmgstate bmg;		// FIXME do this more elegantly if possible
} dfa;

PUBLIC void
init_dfactx(dfactx *dctx,const dfa *space){
	dctx->dfa = space;
	dctx->cur = 0;
}

dfactx *create_dfactx(const dfa *space){
	dfactx *dctx;

	if( (dctx = malloc(sizeof(*dctx))) ){
		init_dfactx(dctx,space);
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
	int max,min;
	unsigned pos;

	min = 0;
	max = node->setsize;
	switch(max){
		case 2: if(node->set[1].label >= s) max = 1; break;
		case 1: if(node->set[0].label >= s) max = 0; break;
		case 0: return max;
	}
	pos = max / 2;
	do{
		if(s < node->set[pos].label){
			max = pos;
			pos = min + (max - min) / 2;
		}else if(s > node->set[pos].label){
			min = pos + 1;
			pos = min + (max - min) / 2;
		}else{
			min = max;
		}
	}while(min < max);
	return pos;
}

static inline unsigned
is_prefix(const unsigned char *word,int wordlen,int pos){
	unsigned suffixlen = wordlen - pos,z;

	for(z = 0 ; z < suffixlen; ++z){
		if(word[z] != word[pos + z]){
			return 0;
		}
	}
	return 1;
}
 
static inline unsigned
suffix_length(const unsigned char *s,unsigned len,unsigned pos){
	unsigned z;

	for(z = 0 ; (s[pos - z] == s[len - 1 - z]) && (z < pos); ++z);
	return z;
}
 
static inline void
make_delta2(int *delta2, const unsigned char *pat, int32_t patlen) {
	int p;
	int last_prefix_index = patlen-1;

	for(p = patlen - 1 ; p >= 0 ; --p){
		if(is_prefix(pat, patlen, p+1)){
			last_prefix_index = p + 1;
		}
		delta2[p] = last_prefix_index + (patlen - 1 - p);
	}
	for(p = 0 ; p < patlen - 1 ; ++p){
		int slen = suffix_length(pat,patlen,p);

		if (pat[p - slen] != pat[patlen - 1 - slen]) {
			delta2[patlen - 1 - slen] = patlen - 1 - p + slen;
		}
	}
}

static int
bmgprepare(bmgstate *bmg,const char *str,void *val){
	unsigned z;

	if((bmg->match = (unsigned char *)strdup(str)) == NULL){
		return -1;
	}
	bmg->len = strlen(str);
	if((bmg->delta2 = malloc(bmg->len * sizeof(*bmg->delta2))) == NULL){
		free(bmg->match);
		return -1;
	}
	bmg->val = val;
	for(z = 0 ; z < 1u << (CHAR_BIT - 1) ; ++z){
		bmg->delta1[z] = bmg->len;
	}
	for(z = 0 ; z < bmg->len - 1 ; ++z){
		bmg->delta1[bmg->match[z]] = bmg->len - 1 - z;
	}
	make_delta2(bmg->delta2,bmg->match,bmg->len);
	return 0;
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
		(*space)->vtxalloc = 1024;
		if(((*space)->vtxarray = malloc(sizeof(*(*space)->vtxarray) * (*space)->vtxalloc)) == NULL){
			free(*space);
			return -1;
		}
		(*space)->vtxarray[0].setsize = 0;
		(*space)->vtxarray[0].set = NULL;
		(*space)->vtxarray[0].val = NULL;
		(*space)->bmg.match = NULL;
		(*space)->patcount = 0;
		(*space)->vtxcount = 1;
		(*space)->longest = 1;
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

			if(pos >= cur->setsize){
				pos = cur->setsize;
			}
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
					sizeof(*cur->set) * (cur->setsize - pos));
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
	if(s - str > (*space)->longest){
		(*space)->longest = s - str;
	}
	if(++(*space)->patcount == 1){
		bmgprepare(&(*space)->bmg,str,val);
	}else if((*space)->bmg.match){
		free((*space)->bmg.match);
		(*space)->bmg.match = NULL;
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

void *match_dfactx_string(dfactx *dctx,const char *str){
	while(*str){
		unsigned pos;

		pos = edge_search(&dctx->dfa->vtxarray[dctx->cur],*str);
		if(pos >= dctx->dfa->vtxarray[dctx->cur].setsize ||
				dctx->dfa->vtxarray[dctx->cur].set[pos].label != *str){
			init_dfactx(dctx,dctx->dfa);
			return NULL;
		}
		++str;
		dctx->cur = dctx->dfa->vtxarray[dctx->cur].set[pos].vtx;
	}
	return dctx->dfa->vtxarray[dctx->cur].val;
}

void *match_dfactx_nstring(dfactx *dctx,const char *s,size_t len){
	while(len--){
		unsigned pos;

		pos = edge_search(&dctx->dfa->vtxarray[dctx->cur],*s);
		if(pos >= dctx->dfa->vtxarray[dctx->cur].setsize ||
				dctx->dfa->vtxarray[dctx->cur].set[pos].label != *s){
			init_dfactx(dctx,dctx->dfa);
			return NULL;
		}
		++s;
		dctx->cur = dctx->dfa->vtxarray[dctx->cur].set[pos].vtx;
	}
	return dctx->dfa->vtxarray[dctx->cur].val;
}

int bmg_against_nstring(const unsigned char *s,size_t len,const bmgstate *bmg){
	size_t i;

	i = bmg->len - 1;
	while (i < len) {
		int j = bmg->len - 1;
		while (j >= 0 && (s[i] == bmg->match[j])) {
			--i;
			--j;
		}
		if (j < 0) {
			return 1;
		}

		i += bmg->delta1[s[i]] >= bmg->delta2[j] ? 
			bmg->delta1[s[i]] : bmg->delta2[j];
	}
	return 0;
}

// FIXME now that apt-file is matching against blocks of text, we need follow
// the sigma function on the edge-not-found case
void *match_dfactx_against_nstring(dfactx *dctx,const char *s,size_t len){
	if(dctx->dfa->bmg.match){
		if(bmg_against_nstring((const unsigned char *)s,len,&dctx->dfa->bmg)){
			return dctx->dfa->bmg.val;
		}
		return NULL;
	}
	while(len--){
		unsigned pos;

		if(dctx->dfa->vtxarray[dctx->cur].val){
			return dctx->dfa->vtxarray[dctx->cur].val;
		}
		pos = edge_search(&dctx->dfa->vtxarray[dctx->cur],*s);
		if(pos >= dctx->dfa->vtxarray[dctx->cur].setsize ||
				dctx->dfa->vtxarray[dctx->cur].set[pos].label != *s){
			init_dfactx(dctx,dctx->dfa);
		}else{
			dctx->cur = dctx->dfa->vtxarray[dctx->cur].set[pos].vtx;
		}
		++s;
	}
	return NULL;
}

static int
recurse_dfa(const dfa *d,const dfavtx *dvtx,char *str,unsigned stroff,
			int (*cb)(const char *,const void *,const void *),
						const void *opaq){
	unsigned e;

	if(dvtx->val){
		str[stroff] = '\0';
		if(cb(str,dvtx->val,opaq)){
			return -1;
		}
	}
	for(e = 0 ; e < dvtx->setsize ; ++e){
		str[stroff] = dvtx->set[e].label;
		if(recurse_dfa(d,d->vtxarray + dvtx->set[e].vtx,str,
					stroff + 1,cb,opaq)){
			return -1;
		}
	}
	return 0;
}

int walk_dfa(const dfa *d,int (*cb)(const char *,const void *,const void *),
					const void *opaq){
	if(d){ // New DFAs get longest = 1, so needn't check that
		char str[d->longest];

		return recurse_dfa(d,d->vtxarray,str,0,cb,opaq);
	}
	return 0;
}
