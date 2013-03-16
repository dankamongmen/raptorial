#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include <getopt.h>
#include <pthread.h>
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
	fprintf(out," -a|--allversions        Print all available versions\n");
	fprintf(out," -h|--help               Display this usage summary\n");
}

static int
all_output(const char **argv,const struct pkgcache *pc,
			const struct pkglist *stat){
	do{
		const struct pkglist *pl;
		const struct pkgobj *po;

		if((po = pkglist_find(stat,*argv)) == NULL){
			if(printf("%s not installed\n",*argv) < 0){
				return -1;
			}
		}else{
			if(printf("%s %s %s\n",*argv,pkgobj_version(po),
						pkgobj_status(po)) < 0){
				return -1;
			}
		}
		for(pl = pkgcache_begin(pc) ; pl ; pl = pkgcache_next(pl)){
			if( (po = pkglist_find(pl,*argv)) ){
				if(printf("%s %s %s %s\n",*argv,
						pkgobj_version(po),
						pkglist_dist(pl),
						pkglist_uri(pl)) < 0){
					return -1;
				}
			}
		}
	}while(*++argv);
	return 0;
}

static int
filtered_output(const char **argv,const struct pkgcache *pc,
				const struct pkglist *stat){
	do{
		const struct pkgobj *po;

		if((po = pkglist_find(stat,*argv)) == NULL){
			if(printf("%s not installed\n",*argv) < 0){
				return -1;
			}
		}else{
			const struct pkgobj *newpo;
			const struct pkglist *pl;

			if((newpo = pkgcache_find_newest(pc,*argv,&pl)) == NULL){
				newpo = po;
				pl = stat;
			}
			if(strcmp(pkgobj_version(newpo),pkgobj_version(po)) == 0){
				if(printf("%s/%s upgradeable from %s to %s\n",
							*argv,pkglist_dist(pl),
							pkgobj_version(po),
							pkgobj_version(newpo)) < 0){
					return -1;
				}
			}else if(printf("%s/%s uptodate %s\n",*argv,pkglist_dist(pl),
						pkgobj_version(po)) < 0){
				return -1;
			}
		}
	}while(*++argv);
	return 0;
}

static int
installed_output(const struct pkgcache *pc,const struct pkglist *stat){
	const struct pkgobj *po;

	for(po = pkglist_begin(stat) ; po ; po = pkglist_next(po)){
		const struct pkgobj *newpo;
		const struct pkglist *pl;

		if((newpo = pkgcache_find_newest(pc,pkgobj_name(po),&pl)) == NULL){
			if(printf("%s %s %s: No available version in archive\n",
					pkgobj_name(po),pkgobj_version(po),
					pkgobj_status(po)) < 0){
				return -1;
			}
		}else if(printf("%s/%s %s %s\n",pkgobj_name(po),pkglist_dist(pl),
				pkgobj_status(po),pkgobj_version(po)) < 0){
			return -1;
		}
	}
	return 0;
}

static void *
par_parse_status_file(void *statusfile){
	struct pkglist *stat;
	int err;

	if((stat = parse_status_file(statusfile,&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",
			(const char *)statusfile,strerror(err));
	}
	return stat;
}

// There's no need to free up the structures on exit -- the OS reclaims that
// memory. If this code is embedded elsewhere, however, make use of
// free_package_list() and free_package_cache() as appropriate.
int main(int argc,char **argv){
	const struct option longopts[] = {
		{ "status-file", 1, NULL, 's' },
		{ "list-dir", 1, NULL, 'l' },
		{ "allversions", 0, NULL, 'a' },
		{ "help", 0, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	const char *listdir;
	struct pkglist *stat;
	int allversions = 0;
	struct pkgcache *pc;
	char *statusfile;
	pthread_t tid;
	int err,c;

	listdir = NULL;
	statusfile = NULL;
	while((c = getopt_long(argc,argv,"s:l:ah",longopts,&optind)) != -1){
		switch(c){
			case 'h':
				usage(stdout,argv[0]);
				return EXIT_SUCCESS;
			case 'a':
				allversions = 1;
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
		if((statusfile = strdup(raptorial_def_status_file())) == NULL){
			fprintf(stderr,"Couldn't duplicate status file path (%s?)\n",strerror(errno));
			return EXIT_FAILURE;
		}
	}
	if(listdir == NULL){
		listdir = raptorial_def_lists_dir();
	}
	if(pthread_create(&tid,NULL,par_parse_status_file,statusfile)){
		fprintf(stderr,"Couldn't launch status-lexing thread\n");
		return EXIT_FAILURE;
	}
	if((pc = parse_packages_dir(listdir,&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",listdir,strerror(err));
		return EXIT_FAILURE;
	}
	if(pthread_join(tid,(void **)&stat)){
		fprintf(stderr,"Couldn't join status-lexing thread\n");
		return EXIT_FAILURE;
	}
	if(stat == NULL){
		return EXIT_FAILURE;
	}
	if(argv[optind]){
		if(allversions){
			if(all_output((const char **)(argv + optind),pc,stat) < 0){
				return EXIT_FAILURE;
			}
		}else if(filtered_output((const char **)(argv + optind),pc,stat) < 0){
			return EXIT_FAILURE;
		}
	}else{
		if(installed_output(pc,stat) < 0){
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
