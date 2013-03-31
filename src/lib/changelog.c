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

static const changelog *
lex_changelog_map(const char *map,size_t len,const changelog **cptr){
	enum {
		STATE_RESET,
		STATE_SOURCE,
		STATE_VERSION_LPAREN,
		STATE_VERSION,
		STATE_VERSION_RPAREN,
		STATE_DIST,
		STATE_DISTDELIM,
		STATE_URGENCY,
		STATE_URGDELIM,
		STATE_URGLINE,
		STATE_CHANGES,
		STATE_MAINT,
		STATE_DATE,
	} state = STATE_RESET;
	const char *source,*version,*dist,*urg,*maint,*changes,*endchange;
	size_t pos,slen,vlen,dlen,ulen,mlen;
	changelog *cl,**enq,*head;

	source = NULL; slen = 0;
	version = NULL; vlen = 0;
	dist = NULL; dlen = 0;
	urg = NULL; ulen = 0;
	maint = NULL; mlen = 0;
	changes = NULL; endchange = NULL;

	cl = NULL;
	head = NULL;
	enq = &head;
	for(pos = 0 ; pos < len ; ++pos){
	switch(state){
		case STATE_RESET:
			if(isspace(map[pos])){
				break;
			}
			if((cl = malloc(sizeof(*cl))) == NULL){
				goto err;
			}
			memset(cl,0,sizeof(*cl));
			state = STATE_SOURCE;
			changes = &map[pos];
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
				// To match the behavior of dpkg-parsechangelog(1), we
				// have to just abort when we hit an invalid line in
				// this state. This case is active in e.g. mime-support
				// and tex-common.
				free_changelog(cl);
				return head;
			}
			++slen;
			break;
		case STATE_VERSION_LPAREN:
			if(isspace(map[pos])){
				break;
			}
			if(map[pos] == '('){
				state = STATE_VERSION;
				version = &map[pos + 1];
				vlen = 0;
				break;
			}
			// To match the behavior of dpkg-parsechangelog(1), we
			// have to just abort when we hit an invalid line in
			// this state. This behavior is pretty broad; see
			// dpkg-parsechangelog(1) output for e.g. junit and gzip.
			free_changelog(cl);
			return head;
			break;
		case STATE_VERSION:
			if(map[pos] == ')'){
				if((cl->version = strndup(version,vlen)) == NULL){
					goto err;
				}
				state = STATE_VERSION_RPAREN;
				break;
			}
			if(!isdebverchar(map[pos])){
				// Done to match dpkg-parsechangelog(1) behavior :/.
				// See apache's changelog for an active instance.
				if(map[pos] == '_'){
					fprintf(stderr,"Warning: invalid version character '%c'\n",map[pos]);
				}else{
					fprintf(stderr,"Expected version, got %.*s\n",(int)(len - pos),map + pos);
					goto err;
				}
			}
			++vlen;
			break;
		case STATE_VERSION_RPAREN:
			if(isspace(map[pos])){
				break;
			}
			state = STATE_DIST;
			dist = &map[pos];
			dlen = 0;
			// intentional fallthrough
		case STATE_DIST: // There can be more than one! (e.g. "frozen unstable")
			if(map[pos] == ';'){
				if(dlen){
					if((cl->dist = strndup(dist,dlen)) == NULL){
						goto err;
					}
				}
				state = STATE_DISTDELIM;
			}else if(map[pos] == '\n' || map[pos] == ':'){
				// See binutils's changelog for examples with no text, just a colon
				// see dpkg's changelog for token without semicolon (e.g. "ALPHA")
				if(dlen){
					if((cl->dist = strndup(dist,dlen)) == NULL){
						goto err;
					}
				}
				state = STATE_CHANGES;
				break;
			}else if(!isblank(map[pos]) && !isdebdistchar(map[pos])){
				fprintf(stderr,"Expected distribution, got %.*s\n",(int)(len - pos),map + pos);
				goto err;
			}else{
				++dlen;
				break;
			}
			// intentional fallthrough for space/';'
		case STATE_DISTDELIM:
			if(isspace(map[pos])){
				break;
			}else if(map[pos] != ';'){
				fprintf(stderr,"Expected ';', got %.*s\n",(int)(len - pos),map + pos);
				goto err;
			}
			state = STATE_URGENCY;
			break;
		case STATE_URGENCY:
			if(isspace(map[pos])){
				break;
			}
			if(len - pos < strlen("urgency=")){
				fprintf(stderr,"Expected 'urgency=', got %.*s\n",(int)(len - pos),map + pos);
				goto err;
			}
			if(memcmp(map + pos,"urgency=",strlen("urgency="))){
				if(len - pos < strlen("priority=")){
					fprintf(stderr,"Expected 'urgency=', got %.*s\n",(int)(len - pos),map + pos);
					goto err;
				}
				if(memcmp(map + pos,"priority=",strlen("priority="))){
					fprintf(stderr,"Expected 'urgency=', got %.*s\n",(int)(len - pos),map + pos);
					goto err;
				}
				pos += strlen("priority=");
			}else{
				pos += strlen("urgency=");
			}
			state = STATE_URGDELIM;
			urg = &map[pos];
			ulen = 0;
			// intentional fallthrough
		case STATE_URGDELIM:
			if(isspace(map[pos])){
				// check urgency for validity FIXME
				if((cl->urg = strndup(urg,ulen)) == NULL){
					goto err;
				}
				state = STATE_URGLINE;
				break;
			}
			++ulen;
			break;
		case STATE_URGLINE:
			// There can be various crap following the urgency (see dpkg's changelog)
			if(map[pos] == '\n'){
				state = STATE_CHANGES;
				break;
			}
			break;
		case STATE_CHANGES:
			if(map[pos] == '\n'){
				if(!endchange){
					endchange = &map[pos]; // FIXME
				}
				if(len - pos >= 5){
					if(memcmp(&map[pos + 1]," -- ",4) == 0){
						state = STATE_MAINT;
						pos += 5;
						maint = &map[pos];
						mlen = 0;
					}
				}
			}
			break;
		case STATE_MAINT:
			if(map[pos] == '>'){ // FIXME do better checking
				state = STATE_DATE;
				if((cl->maintainer = strndup(maint,mlen + 2)) == NULL){
					goto err;
				}
			}else if(map[pos] == '\n'){
				fprintf(stderr,"Expected maintainer, got %.*s\n",(int)(len - pos),map + pos);
				goto err;
			}else{
				++mlen;
			}
			break;
		case STATE_DATE:
			if(isspace(map[pos])){
				if(maint + mlen + 2 == &map[pos]){
					++mlen;
				}else if(map[pos] == '\n'){
					if((cl->date = strndup(maint + mlen + 2,pos - (maint - map) - mlen - 2)) == NULL){
						goto err;
					}
					if((cl->text = strndup(changes,endchange - changes)) == NULL){
						goto err;
					}
					state = STATE_RESET;
					*enq = cl;
					enq = &cl->next;
					cl = NULL;
					endchange = NULL;
				}
			}
			break;
		default:
			goto err;
	}
	}
	if(head == NULL){
		fprintf(stderr,"Empty file\n");
	}
	return (*cptr = head);

err:
	// Don't free the stack of changelog entries; return it
	// through *cptr so callers can use it if they want.
	free_changelog(cl);
	return (*cptr = head); // head != cl here
}

// Behavior of dpkg-parsechangelog(1) is to return all changelog
// entries newer than one on which it blows up, rather than
// erroring out. We allow the caller to decide.
const changelog *lex_changelog(const char *fn,int *err,const changelog **cptr){
	const changelog *dontcare;
	const changelog *cl;
	size_t len;
	void *map;
	int fd;

	if(cptr){
		*cptr = NULL;
	}else{
		cptr = &dontcare;
	}
	if((map = mapit(fn,&len,&fd,0,err)) == MAP_FAILED){
		return NULL;
	}
	if((cl = lex_changelog_map(map,len,cptr)) == NULL){
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

const changelog *
changelog_getnext(const changelog *cl){
	return cl->next;
}
