#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <config.h>
#include <string.h>
#include <getopt.h>
#include <raptorial.h>

static void
usage(const char *name,int retcode){
	FILE *fp = (retcode == EXIT_SUCCESS) ? stdout : stderr;

	fprintf(fp,"rapt-parsechangelog v%s by nick black <dank@qemfd.net>\n",PACKAGE_VERSION);
	fprintf(fp," invoked as %s\n",name);
	fprintf(fp,"\n");
	fprintf(fp,"usage: rapt-parsechangelog [ options ]\n");
	fprintf(fp,"options:\n");
	fprintf(fp,"\t-lchangelog: changelog to parse (default: %s)\n",raptorial_def_changelog());
	fprintf(fp,"\t--all: print all entries\n");
	fprintf(fp,"\t-h/--help: this output\n");
	exit(retcode);
}

// Match the output of dpkg-parsechangelogs exactly.
static char *
format_changes(const char *changes){
	const char *cline = changes;
	char *s = NULL,*tmp;
	size_t len = 0;

	while(*cline){
		const char *nl;
		size_t ll;

		if((nl = strchr(cline,'\n')) == NULL){
			nl = cline + strlen(cline);
		}
		ll = nl - cline + 1;
		if((tmp = realloc(s,len + ll + 1)) == NULL){
			free(s);
			return NULL;
		}
		s = tmp;
		s[len] = ' ';
		memcpy(s + len + 1,cline,ll);
		len += ll;
		cline += ll;
	}
	if((tmp = realloc(s,len + 1)) == NULL){
		free(s);
		return NULL;
	}
	s = tmp;
	s[len] = '\0';
	return s;
}

int main(int argc,char **argv){
	const struct option longopts[] = {
                { "help", 0, NULL, 'h' },
		{ "all", 0, NULL, 'a' },
                { NULL, 0, NULL, 0 }
        };
	const char *clog = NULL;
	const struct changelog *cl,*failed,*last;
	int c,err,all;

	all = 0;
	while((c = getopt_long(argc,argv,"hl:",longopts,&optind)) != -1){
		switch(c){
		case 'a':
			if(all){
				fprintf(stderr,"Set --all twice! Exiting\n");
				usage(argv[0],EXIT_FAILURE);
			}
			all = 1;
			break;
		case 'l':
			if(clog){
				fprintf(stderr,"Set changelog twice! Exiting\n");
				usage(argv[0],EXIT_FAILURE);
			}
			clog = optarg;
			break;
		case 'h':
			usage(argv[0],EXIT_SUCCESS);
			break;
		default:
			fprintf(stderr,"Unknown option: %c\n",c);
			usage(argv[0],EXIT_FAILURE);
			break;
		}
	}
	if(clog == NULL){
		clog = raptorial_def_changelog();
	}
	if(argv[optind]){
		usage(argv[0],EXIT_FAILURE);
	}
	if((cl = lex_changelog(clog,&err,&failed)) == NULL){
		if(!failed){
			fprintf(stderr,"Error lexing changelog \"%s\" (%s?)\n",
					clog,strerror(errno));
			return EXIT_FAILURE;
		}
		fprintf(stderr,"Warning: couldn't lex all of %s\n",clog);
		cl = failed;
	}
	if(all){
		last = NULL;
	}else{
		last = changelog_getnext(cl);
	}
	while(cl != last){
		char *fmt;

		if(printf("Source: %s\nVersion: %s\nDistribution: %s\nUrgency: %s\n"
					"Maintainer: %s\nDate: %s\nChanges:\n%s\n",
					changelog_getsource(cl),
					changelog_getversion(cl),
					changelog_getdist(cl),
					changelog_geturg(cl),
					changelog_getmaintainer(cl),
					changelog_getdate(cl),
					(fmt = format_changes(changelog_getchanges(cl)))) < 0){
			return EXIT_FAILURE;
		}
		free(fmt);
		cl = changelog_getnext(cl);
	}
	return EXIT_SUCCESS;
}
