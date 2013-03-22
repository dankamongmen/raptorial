#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include <getopt.h>
#include <pthread.h>
#include <raptorial.h>

// Our output is a bit funky at times, because we're attempting to faithfully
// emulate apt-show-versions(1). For now.

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

struct focmarsh {
	const struct dfa *dfa;
	const struct pkgcache *pc;
	const struct pkglist *stat;
};

static int
all_output_callback(const char *str,const void *peropaq,
		const void *opaque __attribute__ ((unused))){
	const struct pkgobj *statpkg = peropaq;
	const struct pkgobj *po;

	for(po = pkgobj_matchbegin(statpkg) ; po ; po = pkgobj_matchnext(po)){
		if(printf("%s %s %s %s\n",str,pkgobj_version(po),
			pkgobj_dist(po),pkgobj_uri(po)) < 0){
				return -1;
		}
	}
	return 0;
}

static int
filtered_output_callback(const char *str,const void *peropaq,
		const void *opaque __attribute__ ((unused))){
	const struct pkgobj *po = peropaq;
	const struct pkgobj *newpo;

	if(pkgobj_version(po) == NULL){
		if((newpo = pkgcache_find_newest(po)) == NULL){
			if(printf("%s is neither installed nor available\n",str) < 0){
				return -1;
			}
		}else if(printf("%s is not installed (%s available from %s)\n",
				str,pkgobj_version(newpo),pkgobj_dist(newpo)) < 0){
			return -1;
		}
	}else{
		if((newpo = pkgcache_find_newest(po)) == NULL){
			if(printf("%s %s is installed (unavailable)\n",
						str,pkgobj_version(po)) < 0){
				return -1;
			}
		}else if(debcmp(pkgobj_version(newpo),pkgobj_version(po)) > 0){
			if(printf("%s/%s upgradeable from %s to %s\n",
						str,pkgobj_dist(newpo),
						pkgobj_version(po),
						pkgobj_version(newpo)) < 0){
				return -1;
			}
		}else if(printf("%s/%s uptodate %s\n",str,pkgobj_dist(newpo),
					pkgobj_version(po)) < 0){
			return -1;
		}
	}
	return 0;
}

static int
all_output(const struct dfa *dfa,const struct pkgcache *pc,const struct pkglist *stat){
	const struct focmarsh foc = {
		.pc = pc,
		.stat = stat,
		.dfa = dfa,
	};

	return walk_dfa(dfa,all_output_callback,&foc);
}

static int
installed_output(const struct dfa *dfa,const struct pkgcache *pc,const struct pkglist *stat){
	const struct focmarsh foc = {
		.pc = pc,
		.stat = stat,
		.dfa = dfa,
	};

	return walk_dfa(dfa,filtered_output_callback,&foc);
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
	const char *statusfile,*listdir;
	struct pkglist *stat;
	int allversions = 0;
	struct pkgcache *pc;
	struct dfa *dfa;
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
	statusfile = statusfile;
	dfa = NULL;
	argv += optind;
	while(*argv){
		struct pkgobj *po;

		if((po = create_stub_package(*argv,&err)) == NULL){
			fprintf(stderr,"Couldn't create stub package %s (%s?)\n",
				*argv,strerror(err));
			return EXIT_FAILURE;
		}
		if(augment_dfa(&dfa,*argv,po)){
			fprintf(stderr,"Failure adding %s to DFA\n",*argv);
			return EXIT_FAILURE;
		}
		++argv;
	}
	// We used to parallelize status file reading against the (already
	// parallel) package list reading. We no longer do so, since we
	// generate the filtering DFA based off the status file, and would
	// otherwise need gross locking.
	if((stat = lex_status_file(statusfile,&err,&dfa)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",
			statusfile,strerror(err));
		return EXIT_FAILURE;
	}
	if(dfa){ // otherwise, no packages installed and none listed
		if((pc = lex_packages_dir(listdir,&err,dfa)) == NULL){
			fprintf(stderr,"Couldn't parse %s (%s?)\n",listdir,strerror(err));
			return EXIT_FAILURE;
		}
		if(stat == NULL){
			return EXIT_FAILURE;
		}
		if(allversions){
			if(all_output(dfa,pc,stat) < 0){
				return EXIT_FAILURE;
			}
		}else if(installed_output(dfa,pc,stat) < 0){
			return EXIT_FAILURE;
		}
	}else{
		fprintf(stderr,"No packages installed\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
