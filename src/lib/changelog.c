#include <util.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <raptorial.h>

typedef struct changelog {
	char *source;		// source package
	char *version;		// most recent
	char *urg,*dist;
	char *date;
	char *maintainer;
	char *text;
	struct changelog *next;	// older
} changelog;

static void
free_changelog(changelog *cl){
	while(cl){
		changelog *tmp = cl->next;

		free(cl->maintainer);
		free(cl->version);
		free(cl->source);
		free(cl->text);
		free(cl->dist);
		free(cl->date);
		free(cl->urg);
		free(cl);
		cl = tmp;
	}
}

static int
lex_changelog_map(changelog *cl,const char *map,size_t len){
	enum {
		STATE_RESET,
		STATE_SOURCE,
		STATE_VERSION_LPAREN,
		STATE_VERSION,
		STATE_VERSION_RPAREN,
	} state = STATE_RESET;
	const char *source;
	size_t pos,slen;

	source = NULL; slen = 0;

	memset(cl,0,sizeof(*cl));
	for(pos = 0 ; pos < len ; ++pos){
	switch(state){
		case STATE_RESET:
			if(isspace(map[pos])){
				break;
			}
			state = STATE_SOURCE;
			source = &map[pos];
			slen = 0;
			// intentional fallthrough
		case STATE_SOURCE:
			if(isspace(map[pos])){
				if((cl->source = strndup(source,slen)) == NULL){
					goto err;
				}
				state = STATE_VERSION_LPAREN;
				break;
			}
			if(!isdebpkgchar(map[pos])){
				goto err;
			}
			++slen;
			break;
		case STATE_VERSION_LPAREN:
			break;
		default:
			goto err;
	}
	}
	return 0;

err:
	free_changelog(cl);
	return -1;
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
	return cl->version;
}

const char *
changelog_getdist(const changelog *cl){
	return cl->dist;
}

const char *
changelog_geturg(const changelog *cl){
	return cl->urg;
}

const char *
changelog_getmaintainer(const changelog *cl){
	return cl->maintainer;
}

const char *
changelog_getdate(const changelog *cl){
	return cl->date;
}

const char *
changelog_getchanges(const changelog *cl){
	return cl->text;
}
