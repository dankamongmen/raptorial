#include <aac.h>
#include <zlib.h>
#include <errno.h>
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

#include <stdio.h>
static int
lex_content_map(void *map,off_t inlen,struct dfa *dfa){
	z_stream zstr;
	int z;

	zstr.next_in = map;
	zstr.avail_in = inlen;
	zstr.zalloc = alloc2p;
	zstr.zfree = free1p;
	zstr.opaque = NULL;
	if(inflateInit(&zstr) != Z_OK){
		fprintf(stderr,"Couldn't prepare for inflation\n");
		return -1;
	}
	assert(dfa);
	while((z = inflate(&zstr,Z_FINISH)) == Z_STREAM_END){
		//dfactxdctx;
		if(z != Z_OK){
			fprintf(stderr,"Difficulties inflating (%d)\n",z);
			return -1;
		}
	}
	if(inflateEnd(&zstr) != Z_OK){
		fprintf(stderr,"Couldn't finalize inflation\n");
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
