#include <aac.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <blossom.h>
#include <sys/types.h>

struct dirparse {
	DIR *dir;
	struct dfa *dfa;
};

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
		// FIXME decompress and read, matching against dfa
	}
	// *err = errno;
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
