#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include <getopt.h>
#include <raptorial.h>

static void
usage(FILE *out,const char *name){
	fprintf(out,"rapt-show-versions v%s by nick black <dank@qemfd.net>\n",PACKAGE_VERSION);
	fprintf(out," invoked as %s\n",name);
	fprintf(out,"\n");
	fprintf(out,"usage: rapt-show-versions [ options ] packageregex\n");
	fprintf(out,"options:\n");
	fprintf(out," -s|--status-file=<file> Status file (def: %s)\n",raptorial_def_status_file());
	fprintf(out," -l|--list-dir=<dir>     List directory (def: %s)\n",raptorial_def_lists_dir());
	fprintf(out," -v|--verbose            Increase verbosity during parsing\n");
	fprintf(out," -a|--allversions        Print all available versions\n");
	fprintf(out," -h|--help               Display this usage summary\n");
}

int main(int argc,char **argv){
	const struct option longopts[] = {
		{ "status-file", 1, NULL, 's' },
		{ "list-dir", 1, NULL, 'l' },
		{ "verbose", 0, NULL, 'v' },
		{ "allversions", 0, NULL, 'a' },
		{ "help", 0, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	const char *listdir,*statusfile;
	struct pkgcache *pc;
	struct pkglist *pl;
	struct pkgobj *po;
	int err,c;

	//verbose = 0;
	listdir = NULL;
	statusfile = NULL;
	while((c = getopt_long(argc,argv,"s:l:p:vah",longopts,&optind)) != -1){
		switch(c){
			case 'h':
				usage(stdout,argv[0]);
				return EXIT_SUCCESS;
			case 'a':
				fprintf(stderr,"Not yet implemented: --allversions\n");
				return EXIT_FAILURE; // FIXME
			case 'v':
				// verbose = 1;
				break;
			case 'l':
				if(listdir){
					fprintf(stderr,"Provided listdir twice.\n");
					return EXIT_FAILURE;
				}
				listdir = optarg;
				break;
			case 's':
				if(statusfile){
					fprintf(stderr,"Provided status file twice.\n");
					return EXIT_FAILURE;
				}
				statusfile = optarg;
				break;
			default:
				fprintf(stderr,"Unknown option: %c\n",c);
				usage(stderr,argv[0]);
				break;
		}
	}
	if(statusfile == NULL){
		statusfile = raptorial_def_status_file();
	}
	if(listdir == NULL){
		listdir = raptorial_def_lists_dir();
	}
	if(parse_status_file(statusfile,&err)){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",statusfile,strerror(err));
		return EXIT_FAILURE;
	}
	if((pc = parse_packages_dir(listdir,&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",listdir,strerror(err));
		return EXIT_FAILURE;
	}
	for(pl = pkgcache_begin(pc) ; pl ; pl = pkgcache_next(pl)){
		for(po = pkglist_begin(pl) ; po ; po = pkglist_next(po)){
			char **filters = argv + optind;

			if(*filters){
				do{
					if(strcmp(*filters,pkgobj_name(po)) == 0){
						break;
					}
				}while(*++filters);
				if(!*filters){
					continue;
				}
			}
			// Associate with package from status file and print
			// upgrade info as available FIXME
			printf("%s/%s %s\n",pkgobj_name(po),
				pkglist_dist(pl),pkgobj_version(po));
		}
	}
	free_package_cache(pc);
	return EXIT_SUCCESS;
}
