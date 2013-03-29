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
	fprintf(fp,"\t-Fchangelogfmt: changelog format (default: debian)\n");
	fprintf(fp,"\t-h/--help: this output\n");
	exit(retcode);
}

int main(int argc,char **argv){
	const struct option longopts[] = {
                { "help", 0, NULL, 'h' },
                { NULL, 0, NULL, 0 }
        };
	const char *clog = NULL;
	struct changelog *cl;
	int c,err;

	while((c = getopt_long(argc,argv,"hl:F:",longopts,&optind)) != -1){
		switch(c){
		case 'F':
			fprintf(stderr,"Sorry, '-%c' is not yet implemented\n",c);
			exit(EXIT_FAILURE);
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
	if((cl = lex_changelog(clog,&err)) == NULL){
		fprintf(stderr,"Error lexing changelog \"%s\" (%s?)\n",
				clog,strerror(errno));
		return EXIT_FAILURE;
	}
	if(printf("Source: %s\nVersion: %s\nDistribution: %s\nUrgency: %s\n"
				"Maintainer: %s\nDate: %s\nChanges:\n%s\n",
				changelog_getsource(cl),
				changelog_getversion(cl),
				changelog_getdist(cl),
				changelog_geturg(cl),
				changelog_getmaintainer(cl),
				changelog_getdate(cl),
				changelog_getchanges(cl)) < 0){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
