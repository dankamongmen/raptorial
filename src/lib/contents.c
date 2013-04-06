#include <aac.h>
#include <zlib.h>
#include <util.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <blossom.h>
#include <sys/mman.h>
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
	STATE_SINK,
	STATE_INTER,
	STATE_VAL,
};

static int
lex_content(void *vmap,size_t len,const struct dfa *dfa,int *pastheader,int nocase){
	char *map = vmap,*hol,*val;
	size_t off = 0;
	dfactx dctx;
	int s;

	s = STATE_HOL; // Ensure we're always starting on a fresh line! FIXME
	hol = val = NULL;
	while(off < len){
		if(map[off] == '\n'){
			map[off] = '\0';
			if(s == STATE_VAL){
				if(*pastheader){
					printf("%s: /%s\n",val,hol);
				}else if(strcmp(hol,"FILE") == 0 && strcmp(val,"LOCATION") == 0){
					*pastheader = 1;
				}
			}
			hol = map + off;
			s = STATE_HOL;
			++off;
			continue;
		}
		switch(s){
		case STATE_HOL:
			if(isspace(map[off])){ // stay in STATE_HOL
				break;
			}
			hol = map + off;
			s = STATE_MATCHING;
			if(nocase && *pastheader){
				map[off] = tolower(map[off]);
			}
			break;
		case STATE_MATCHING:
			if(isspace(map[off])){
				init_dfactx(&dctx,dfa);
				if(!*pastheader || match_dfactx_against_nstring(&dctx,hol,map + off - hol)){
					s = STATE_INTER;
					map[off] = '\0';
					val = map + off;
				}else{
					s = STATE_SINK;
					val = NULL;
				}
			}else if(nocase && *pastheader){
				map[off] = tolower(map[off]);
			}
			break;
		case STATE_INTER:
			if(*pastheader){
				if(map[off] == '/'){
					s = STATE_VAL;
					val = map + off + 1;
				}
			}else if(!isspace(map[off])){
				s = STATE_VAL;
				val = map + off;
			}
			break;
			// we only care about SINK/VAL when they get a
			// newline, which is handled at the beginning.
		}
		++off;
	}
	return 0;
}

// Paralellizing across the directory is of limited utility; compressed file
// sizes vary by several orders of magnitude. If we can't finish our file
// ourselves, place the zlib context on a work queue, and let threads fall
// back to that.
typedef struct workmonad {
	void *map;
	size_t len;
	z_stream zstr;
	struct workmonad *next;
} workmonad;

// We need some mechanism to keep threads from exiting early, when the last
// files are still in _oneshot(). Prior to calling readdir_r(), post to the
// semaphore. Decrement the semaphore once we're out of _oneshot() (and,
// possibly, posted a work element). Other threads sleep on the semaphore if
// they can't find actual work. Only after successfully sleeping on the
// semaphore, and checking for newly posted work, can they exit.
struct dirparse {
	DIR *dir;
	int nocase;
	const struct dfa *dfa;

	// The lock governs only queue; dir is synchronized by the kernel.
	workmonad *queue;
	unsigned holdup_sem;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

static inline void
finish_workmonad(workmonad *wm,struct dirparse *dp){
	free(wm);
	pthread_mutex_lock(&dp->lock);
		--dp->holdup_sem;
	pthread_mutex_unlock(&dp->lock);
	pthread_cond_signal(&dp->cond);
}

static inline void
enqueue_workmonad(workmonad *wm,struct dirparse *dp){
	pthread_mutex_lock(&dp->lock);
		wm->next = dp->queue;
		dp->queue = wm;
		--dp->holdup_sem;
	pthread_mutex_unlock(&dp->lock);
	pthread_cond_signal(&dp->cond);
}

static int
lex_content_map_nextshot(workmonad *wm,struct dirparse *dp,
				void *infbuf,size_t buflen){
	int z,ph;

	wm->zstr.next_out = infbuf;
	wm->zstr.avail_out = buflen;
	z = inflate(&wm->zstr,Z_NO_FLUSH);
	if(z != Z_OK && z != Z_STREAM_END){
		inflateEnd(&wm->zstr);
		return -1;
	}
	if(z == Z_OK){ // equivalent to zstr.avail_in == 0
		enqueue_workmonad(wm,dp);
	}
	ph = 1;
	if(lex_content(infbuf,buflen,dp->dfa,&ph,dp->nocase)){
		// FIXME how to free inflate state at this point?
		return -1;
	}
	if(z == Z_STREAM_END){
		if(inflateEnd(&wm->zstr) != Z_OK){
			return -1;
		}
		finish_workmonad(wm,dp);
		return 0;
	}
	return 0;
}

static workmonad *
create_workmonad(void *map,size_t len,const z_stream *zstr,struct dirparse *dp){
	workmonad *wm;

	if( (wm = malloc(sizeof(*wm))) ){
		memcpy(&wm->zstr,zstr,sizeof(*zstr));
		wm->len = len;
		wm->map = map;
		enqueue_workmonad(wm,dp);
	}
	return wm;
}

// Try to do the entire file in one go of inflate(). If we can't, exit so that
// we can go on the work queue and get parallelized.
//
// FIXME lift allocation of scratchspace out of here, and keep it across the
// life of the thread
static int
lex_content_map_oneshot(void *map,off_t inlen,struct dirparse *dp,
				void *infbuf,size_t buflen){
	z_stream zstr;
	int z,ph;

	if(inlen <= 0){
		return -1;
	}
	memset(&zstr,0,sizeof(zstr));
	zstr.next_out = infbuf;
	zstr.avail_out = buflen;
	zstr.next_in = map;
	zstr.avail_in = inlen;
	zstr.zalloc = alloc2p;
	zstr.zfree = free1p;
	zstr.opaque = NULL;
	if(inflateInit2(&zstr,47) != Z_OK){
		return -1;
	}
	/*gz_header gzh;
	  if(inflateGetHeader(&zstr,&gzh) != Z_OK){
		fprintf(stderr,"Not a gzip file?\n");
		inflateEnd(&zstr);
		return -1;
	}*/
	// There's a retarded freeform header at the beginning of each content
	// file. See http://wiki.debian.org/RepositoryFormat#A.22Contents.22_indices.
	ph = 0;
	z = inflate(&zstr,Z_NO_FLUSH);
	if(z != Z_OK && z != Z_STREAM_END){
		inflateEnd(&zstr);
		return -1;
	}
	if(z == Z_OK){ // equivalent to zstr.avail_in == 0, no? FIXME
		if(create_workmonad(map,zstr.avail_in,&zstr,dp) == NULL){
			inflateEnd(&zstr);
			return -1;
		}
	}
	ph = 0;
	if(lex_content(infbuf,buflen - zstr.avail_out,dp->dfa,
				&ph,dp->nocase)){
		inflateEnd(&zstr);
		return -1;
	}
	if(!ph){ // FIXME
		fprintf(stderr,"Didn't consume header!\n");
		assert(0);
	}
	if(z == Z_STREAM_END){
		if(inflateEnd(&zstr) != Z_OK){
			return -1;
		}
		finish_workmonad(NULL,dp);
		return 0;
	}
	return 0;
}

static int
lex_packages_file_internal(const char *path,struct dirparse *dp,void *infbuf,
						size_t buflen){
	size_t mlen;
	int fd,err;
	void *map;

	if(path == NULL){
		return -1;
	}
	if((map = mapit(path,&mlen,&fd,1,&err)) == MAP_FAILED){
		return -1;
	}
	if(lex_content_map_oneshot(map,mlen,dp,infbuf,buflen)){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int
lex_workqueue(struct dirparse *dp,void *infbuf,size_t buflen){
	workmonad *wm;

	while(pthread_mutex_lock(&dp->lock) == 0){
		if( (wm = dp->queue) ){
			dp->queue = wm->next;
			++dp->holdup_sem;
		}
		pthread_mutex_unlock(&dp->lock);
		if(wm == NULL){ // FIXME see notes about early exit
			return 0;
		}
		if(lex_content_map_nextshot(wm,dp,infbuf,buflen)){
			return -1;
		}
	}
	return -1; // lock got befouled!
}

static void *
lex_dir(void *vdp){
	struct dirparse *dp = vdp;
	struct dirent dent,*pdent;
	size_t infbuflen;
	unsigned holdup;
	void *infbuf;
	int r;

	infbuflen = 16 * 1024 * 1024; // FIXME aieeee
	if((infbuf = malloc(infbuflen)) == NULL){
		fprintf(stderr,"Couldn't allocate %zu bytes\n",infbuflen);
		return NULL;
	}
	pthread_mutex_lock(&dp->lock);
	++dp->holdup_sem;
	pthread_mutex_unlock(&dp->lock);
	while( (r = readdir_r(dp->dir,&dent,&pdent)) == 0 && pdent){
		const char *ext;

		if(dent.d_type != DT_REG && dent.d_type != DT_LNK){
			continue; // FIXME maybe don't skip DT_UNKNOWN?
		}
		if((ext = strrchr(dent.d_name,'.')) == NULL){
			continue;
		}
		if(strcmp(ext,".gz")){
			continue;
		}
		if(lex_packages_file_internal(dent.d_name,dp,infbuf,infbuflen)){
			free(infbuf);
			return NULL;
		}
		pthread_mutex_lock(&dp->lock);
		++dp->holdup_sem;
		pthread_mutex_unlock(&dp->lock);
	}
	finish_workmonad(NULL,dp);
	if(r){
		free(infbuf);
		return NULL;
	}
	do{
		pthread_mutex_lock(&dp->lock);
		holdup = dp->holdup_sem;
		pthread_mutex_unlock(&dp->lock);
		if(lex_workqueue(dp,infbuf,infbuflen)){
			free(infbuf);
			return NULL;
		}
	}while(holdup);
	free(infbuf);
	return dp;
}

// len is the true length, less than or equal to the mapped length.
static int
lex_listdir(DIR *dir,int *err,struct dfa *dfa,int nocase){
	struct dirparse dp = {
		.dir = dir,
		.dfa = dfa,
		.queue = NULL,
		.holdup_sem = 0,
		.nocase = nocase,
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
	if( (r = pthread_cond_init(&dp.cond,NULL)) ){
		*err = r;
		pthread_mutex_destroy(&dp.lock);
		return -1;
	}
	if(blossom_per_pe(&bctl,&bs,NULL,lex_dir,&dp)){
		*err = errno;
		pthread_mutex_destroy(&dp.lock);
		pthread_cond_destroy(&dp.cond);
		return -1;
	}
	if(blossom_join_all(&bs)){
		*err = errno;
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		pthread_cond_destroy(&dp.cond);
		return -1;
	}
	if(blossom_validate_joinrets(&bs)){
		blossom_free_state(&bs);
		pthread_mutex_destroy(&dp.lock);
		pthread_cond_destroy(&dp.cond);
		return -1;
	}
	blossom_free_state(&bs);
	pthread_mutex_destroy(&dp.lock);
	pthread_cond_destroy(&dp.cond);
	return 0;
}

// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA.
PUBLIC int
lex_contents_dir(const char *dir,int *err,struct dfa *dfa,int nocase){
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
	if(lex_listdir(d,err,dfa,nocase)){
		closedir(d);
		return -1;
	}
	if(closedir(d)){
		return -1;
	}
	return 0;
}
