#include <aac.h>
#include <zlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <blossom.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

static void *
alloc2p(void *opaque __attribute__ ((unused)),unsigned items,unsigned size){
	return malloc(items * size);
}

static void
free1p(void *opaque __attribute__ ((unused)),void *addr){
	free(addr);
}

enum {
	STATE_HOL,
	STATE_MATCHING,
	STATE_MATCHING_MATCHED,
	STATE_INTER,
	STATE_INTER_MATCHED,
	STATE_VAL,
	STATE_VAL_MATCHED,
};

static int
lex_content(const void *vmap,size_t len,struct dfa *dfa){
	const char *map = vmap,*hol;
	size_t off = 0;
	dfactx dctx;
	int s;

	s = STATE_HOL; // Ensure we're always starting on a fresh line! FIXME
	hol = NULL;
	while(off < len){
		switch(s){
		case STATE_HOL:
			if(isspace(map[off])){
				break;
			}
			hol = map + off;
			init_dfactx(&dctx,dfa);
			s = STATE_MATCHING;
			// intentional fallthrough to do first match
		case STATE_MATCHING: case STATE_MATCHING_MATCHED:
			if(isspace(map[off])){
				s = (map[off] == '\n') ? STATE_HOL :
					s == STATE_MATCHING_MATCHED ?
						STATE_INTER_MATCHED : STATE_INTER;
			}else{
				const struct pkgobj *po = match_dfactx_char(&dctx,map[off]);
				if(po){ // FIXME
					s = STATE_MATCHING_MATCHED;
				}
			}
			break;
		case STATE_INTER: case STATE_INTER_MATCHED:
			if(isspace(map[off])){
				if(map[off] == '\n'){
					s = STATE_HOL;
				}
			}else{
				s = s == STATE_INTER ? STATE_VAL : STATE_VAL_MATCHED;
			}
			break;
		case STATE_VAL:
			if(map[off] == '\n'){
				s = STATE_HOL;
			}
			break;
		case STATE_VAL_MATCHED:
			if(map[off] == '\n'){
				printf("%.*s",(int)(off - (hol - map) + 1),hol);
				s = STATE_HOL;
			}
		}
		++off;
	}
	assert(map);
	assert(len);
	assert(dfa);
	return 0;
}

static int
lex_content_map(void *map,off_t inlen,struct dfa *dfa){
	size_t scratchsize;
	z_stream zstr;
	void *scratch;
	int z;

	if(inlen <= 0){
		return -1;
	}
	scratchsize = inlen * 2;
	if((scratch = malloc(scratchsize)) == NULL){
		return -1;
	}
	assert(map);
	memset(&zstr,0,sizeof(zstr));
	zstr.next_out = scratch;
	zstr.avail_out = scratchsize;
	zstr.next_in = map;
	zstr.avail_in = inlen;
	zstr.zalloc = alloc2p;
	zstr.zfree = free1p;
	zstr.opaque = NULL;
	if(inflateInit2(&zstr,47) != Z_OK){
		free(scratch);
		return -1;
	}
	/*gz_header gzh;
	  if(inflateGetHeader(&zstr,&gzh) != Z_OK){
		fprintf(stderr,"Not a gzip file?\n");
		inflateEnd(&zstr);
		return -1;
	}*/
	assert(dfa);
	while((z = inflate(&zstr,0)) != Z_STREAM_END){
		if(z != Z_OK){
			inflateEnd(&zstr);
			free(scratch);
			return -1;
		}
		if(lex_content(scratch,scratchsize - zstr.avail_out,dfa)){
			inflateEnd(&zstr);
			free(scratch);
			return -1;
		}
		zstr.avail_out = scratchsize;
		zstr.next_out = scratch;
	}
	free(scratch);
	if(inflateEnd(&zstr) != Z_OK){
		return -1;
	}
	return 0;
}

struct dirparse {
	DIR *dir;
	struct dfa *dfa;
};

static int
lex_packages_file_internal(const char *path,struct dfa *dfa){
	size_t mlen,len;
	struct stat st;
	void *map;
	int fd,pg;

	if(path == NULL){
		return -1;
	}
	// Probably ought get the largest page size; this will be the smallest
	// on all platforms of which I'm aware. FIXME
	if((pg = sysconf(_SC_PAGE_SIZE)) <= 0){
		return -1;
	}
	if((fd = open(path,O_CLOEXEC)) < 0){
		return -1;
	}
	if(fstat(fd,&st)){
		close(fd);
		return -1;
	}
	mlen = len = st.st_size;
	if(mlen % pg != mlen / pg){
		mlen = (mlen / pg) * pg + pg;
	}
	if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_HUGETLB|MAP_POPULATE,fd,0)) == MAP_FAILED){
		if(errno != EINVAL){
			close(fd);
			return -1;
		}
		// Try again without MAP_HUGETLB
		if((map = mmap(NULL,mlen,PROT_READ,MAP_SHARED|MAP_POPULATE,fd,0)) == MAP_FAILED){
			close(fd);
			return -1;
		}
	}
	if(lex_content_map(map,st.st_size,dfa)){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static void *
lex_dir(void *vdp){
	struct dirparse *dp = vdp;
	struct dirent dent,*pdent;

	while(readdir_r(dp->dir,&dent,&pdent) == 0){
		const char *ext;

		if(pdent == NULL){
			return dp;
		}
		if(dent.d_type != DT_REG && dent.d_type != DT_LNK){
			continue; // FIXME maybe don't skip DT_UNKNOWN?
		}
		if((ext = strrchr(dent.d_name,'.')) == NULL){
			continue;
		}
		if(strcmp(ext,".gz")){
			continue;
		}
		if(lex_packages_file_internal(dent.d_name,dp->dfa)){
			return NULL;
		}
	}
	return NULL;
}

// len is the true length, less than or equal to the mapped length.
static int
lex_listdir(DIR *dir,int *err,struct dfa *dfa){
	struct dirparse dp = {
		.dir = dir,
		.dfa = dfa,
	};
	blossom_ctl bctl = {
		.flags = 0,
		.tids = 1,
	};
	blossom_state bs;

	if(blossom_per_pe(&bctl,&bs,NULL,lex_dir,&dp)){
		*err = errno;
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		return -1;
	}
	if(blossom_validate_joinrets(&bs)){
		blossom_free_state(&bs);
		return -1;
	}
	blossom_free_state(&bs);
	return 0;
}

// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA.
PUBLIC int
lex_contents_dir(const char *dir,int *err,struct dfa *dfa){
	DIR *d;

	if((d = opendir(dir)) == NULL){
		*err = errno;
		return -1;
	}
	// Change directory so that relative dent.d_name entries can be opened
	if(chdir(dir)){
		*err = errno;
		closedir(d);
		return -1;
	}
	if(lex_listdir(d,err,dfa)){
		closedir(d);
		return -1;
	}
	if(closedir(d)){
		return -1;
	}
	return 0;
}
