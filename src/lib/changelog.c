#include <util.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <raptorial.h>

typedef struct clentry {
	char *version;		// most recent
	char *urg,*dist;
	char *date;
	char *maintainer;
	char *text;
	struct clentry *next;	// older
} clentry;

typedef struct changelog {
	char *source;		// source package
	clentry *entries;
} changelog;

static void
free_changelog(changelog *cl){
	if(cl){
		clentry *cle;

		while( (cle = cl->entries) ){
			cl->entries = cle->next;
			free(cle->maintainer);
			free(cle->version);
			free(cle->text);
			free(cle->dist);
			free(cle->date);
			free(cle->urg);
			free(cle);
		}
		free(cl->source);
		free(cl);
	}
}

static int
lex_changelog_map(changelog *cl,const char *map,size_t len){
	/*enum {
		STATE_RESET
	} state = STATE_RESET;*/
	size_t pos;

	memset(cl,0,sizeof(*cl));
	for(pos = 0 ; pos < len ; ++pos){
		switch(map[pos]){
		}
	}
	return 0;
}

changelog *lex_changelog(const char *fn,int *err){
	changelog *cl;
	size_t len;
	void *map;
	int fd;

	if((cl = malloc(sizeof(*cl))) == NULL){
		*err = errno;
		return NULL;
	}
	if((map = mapit(fn,&len,&fd,0,err)) == MAP_FAILED){
		free_changelog(cl);
		return NULL;
	}
	if(lex_changelog_map(cl,map,len)){
		free_changelog(cl);
		close(fd);
		return NULL;
	}
	close(fd);
	return cl;
}

const char *
changelog_getsource(const changelog *cl){
	return cl->source;
}

const char *
changelog_getversion(const changelog *cl){
	return cl->entries ? cl->entries->version : NULL;
}

const char *
changelog_getdist(const changelog *cl){
	return cl->entries ? cl->entries->dist : NULL;
}

const char *
changelog_geturg(const changelog *cl){
	return cl->entries ? cl->entries->urg : NULL;
}

const char *
changelog_getmaintainer(const changelog *cl){
	return cl->entries ? cl->entries->maintainer : NULL;
}

const char *
changelog_getdate(const changelog *cl){
	return cl->entries ? cl->entries->date : NULL;
}

const char *
changelog_getchanges(const changelog *cl){
	return cl->entries ? cl->entries->text : NULL;
}
