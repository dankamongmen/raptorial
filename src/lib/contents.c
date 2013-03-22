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
	pthread_mutex_t lock;
};

static void *
lex_dir(void *vdp){
	struct dirparse *dp = vdp;
	struct dirent dent,*pdent;
	struct dfa **dfap;

	dfap = dp->dfa ? &dp->dfa : NULL;
	assert(dfap);
	while(readdir_r(dp->dir,&dent,&pdent) == 0){
		const char *suffixes[] = { "Sources", "Packages", NULL },**suffix;
		const char *distdelim,*dist,*uridelim;

		if(pdent == NULL){
			return dp;
		}
		if(dent.d_type != DT_REG && dent.d_type != DT_LNK){
			continue; // FIXME maybe don't skip DT_UNKNOWN?
		}
		if((uridelim = strchr(dent.d_name,'_')) == NULL){
			continue;
		}
		if((dist = strstr(uridelim,"_dists_")) == NULL){
			continue;
		}
		dist += strlen("_dists_");
		if((distdelim = strchr(dist,'_')) == NULL){
			continue;
		}
		for(suffix = suffixes ; *suffix ; ++suffix){
			if(strlen(dent.d_name) < strlen(*suffix)){
				continue;
			}
			if(strcmp(dent.d_name + strlen(dent.d_name) - strlen(*suffix),*suffix) == 0){
				/*
				int err;

				if((pl = lex_packages_file_internal(dent.d_name,&err,0,dfap)) == NULL){
					return NULL;
				}
				*/
				pthread_mutex_lock(&dp->lock);
				pthread_mutex_unlock(&dp->lock);
				break;
			}
		}
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
	int r;

	if( (r = pthread_mutex_init(&dp.lock,NULL)) ){
		*err = r;
		return -1;
	}
	if(blossom_per_pe(&bctl,&bs,NULL,lex_dir,&dp)){
		*err = errno;
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	if(blossom_validate_joinrets(&bs)){
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	blossom_free_state(&bs);
	if((r = pthread_mutex_destroy(&dp.lock))){
		*err = r;
		return -1;
	}
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
