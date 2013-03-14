#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "config.h"

static void
usage(const char *name,int retcode){
	FILE *fp = (retcode == EXIT_SUCCESS) ? stdout : stderr;

	fprintf(fp,"raptorial-file v%s by nick black <dank@qemfd.net>\n",PACKAGE_VERSION);
	fprintf(fp," invoked as %s\n",name);
	fprintf(fp,"\n");
	fprintf(fp,"usage: raptorial-file [ options ] action [ pattern ]\n");
	exit(retcode);
}

int main(int argc,char **argv){
	const struct option longopts[] = {
                { "help", 0, NULL, 'h' },
                { NULL, 0, NULL, 0 }
        };
	int c;

	while((c = getopt_long(argc,argv,"h",longopts,&optind)) != -1){
		switch(c){
		case 'h':
			usage(argv[0],EXIT_SUCCESS);
			break;
		default:
			fprintf(stderr,"Unknown option: %c\n",c);
			usage(argv[0],EXIT_FAILURE);
			break;
		}
	}
	if(argv[optind] == NULL){
		usage(argv[0],EXIT_FAILURE);
	}
	return EXIT_SUCCESS;
}
