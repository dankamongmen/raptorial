#include <stdio.h>
#include <paths.h>
#include <ctype.h>
#include <stdlib.h>
#include <getopt.h>
#include "config.h"
#include <string.h>
#include <raptorial.h>

static void
usage(const char *name,int retcode){
	FILE *fp = (retcode == EXIT_SUCCESS) ? stdout : stderr;

	fprintf(fp,"raptorial-file v%s by nick black <dank@qemfd.net>\n",PACKAGE_VERSION);
	fprintf(fp," invoked as %s\n",name);
	fprintf(fp,"\n");
	fprintf(fp,"usage: raptorial-file [ options ] patterns\n");
	exit(retcode);
}

static char *
lowercase(const char *s){
	char *ret;

	if( (ret = malloc((strlen(s) + 1) * sizeof(*ret))) ){
		unsigned z;

		for(z = 0 ; z <= strlen(s) ; ++z){
			ret[z] = tolower(s[z]);
		}
	}
	return ret;
}

int main(int argc,char **argv){
	const struct option longopts[] = {
		{ "architecture", 1, NULL, 'a' },
		{ "cache", 1, NULL, 'c' },
		{ "cdrom-mount", 1, NULL, 'd' },
		{ "from-deb", 0, NULL, 'D' },
		{ "from-file", 1, NULL, 'f' },
		{ "ignore-case", 0, NULL, 'i' },
		{ "non-interactive", 0, NULL, 'N' },
		{ "sources-list", 1, NULL, 's' },
		{ "verbose", 0, NULL, 'v' },
                { "help", 0, NULL, 'h' },
                { NULL, 0, NULL, 0 }
        };
	const char *cdir = NULL;
	int c,err,nocase = 0;
	struct dfa *dfa;

	while((c = getopt_long(argc,argv,"h",longopts,&optind)) != -1){
		switch(c){
		case 'a':
		case 'c':
			if(cdir){
				fprintf(stderr,"Provided -c/--cache twice, exiting\n");
				usage(argv[0],EXIT_FAILURE);
				break;
			}
			cdir = argv[optind];
			break;
		case 'd':
		case 'D':
		case 'f':
		case 'i':
			if(nocase){
				fprintf(stderr,"Provided -i/--ignore-case twice, exiting\n");
				usage(argv[0],EXIT_FAILURE);
				break;
			}
			nocase = 1;
			break;
		case 'N':
		case 's':
		case 'v':
			fprintf(stderr,"Sorry, '%c' is not yet implemented\n",c);
			exit(EXIT_FAILURE);
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
	dfa = NULL;
	if(!cdir){
		cdir = raptorial_def_content_dir();
	}
	if(argv[optind] == NULL){
		fprintf(stderr,"Didn't provide any search terms!\n");
		usage(argv[0],EXIT_FAILURE);
	}
	do{
                struct pkgobj *po;
		char *s;

		if((s = nocase ? lowercase(argv[optind]) : strdup(argv[optind])) == NULL){
			return EXIT_FAILURE;
		}
                if((po = create_stub_package(s,&err)) == NULL){
                        fprintf(stderr,"Couldn't create stub package %s (%s?)\n",
                                argv[optind],strerror(err));
			free(s);
                        return EXIT_FAILURE;
                }
                if(augment_dfa(&dfa,s,po)){
                        fprintf(stderr,"Failure adding %s to dfa\n",argv[optind]);
			free(s);
                        return EXIT_FAILURE;
                }
		free(s);
                ++optind;
	}while(argv[optind]);
	if(lex_contents_dir(cdir,&err,dfa,nocase)){
		fprintf(stderr,"Error matching contents files (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
