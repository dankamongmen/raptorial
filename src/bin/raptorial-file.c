#include <stdio.h>
#include <paths.h>
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

int main(int argc,char **argv){
	const struct option longopts[] = {
		{ "architecture", 1, NULL, 'a' },
		{ "cache", 1, NULL, 'c' },
		{ "cdrom-mount", 1, NULL, 'd' },
		{ "from-deb", 0, NULL, 'D' },
		{ "from-file", 1, NULL, 'f' },
		{ "non-interactive", 0, NULL, 'N' },
		{ "sources-list", 1, NULL, 's' },
		{ "verbose", 0, NULL, 'v' },
                { "help", 0, NULL, 'h' },
                { NULL, 0, NULL, 0 }
        };
	const char *cdir = NULL;
	struct sdfa *dfa;
	int c,err;

	while((c = getopt_long(argc,argv,"h",longopts,&optind)) != -1){
		switch(c){
		case 'a':
		case 'c':
		case 'd':
		case 'D':
		case 'f':
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
	cdir = raptorial_def_content_dir();
	if(argv[optind] == NULL){
		fprintf(stderr,"Didn't provide any search terms!\n");
		usage(argv[0],EXIT_FAILURE);
	}
	dfa = NULL;
	while(*argv){
                struct pkgobj *po;

                if((po = create_stub_package(*argv,&err)) == NULL){
                        fprintf(stderr,"Couldn't create stub package %s (%s?)\n",
                                *argv,strerror(err));
                        return EXIT_FAILURE;
                }
                if(augment_sdfa(&dfa,*argv,po)){
                        fprintf(stderr,"Failure adding %s to sDFA\n",*argv);
                        return EXIT_FAILURE;
                }
                ++argv;
	}
	if(lex_contents_dir(cdir,&err,dfa)){
		fprintf(stderr,"Error matching contents files (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
