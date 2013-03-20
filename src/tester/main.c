#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raptorial.h>

static void
usage(const char *name){
	fprintf(stderr,"usage: %s packagesfile\n",name);
}

int main(int argc,char **argv){
	const struct pkglist *pl;
	const struct pkgobj *po;
	struct pkgcache *pc;
	struct dfa *dfa;
	unsigned pkgs;
	int err;

	if(argc != 2){
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if((pc = pkgcache_from_pkglist(lex_packages_file(argv[1],&err,NULL),&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",argv[1],strerror(err));
		return EXIT_FAILURE;
	}
	dfa = NULL;
	pkgs = 0;
	printf("Reported package count: %u\n",pkgcache_count(pc));
	for(pl = pkgcache_begin(pc) ; pl ; pl = pkgcache_next(pl)){
		for(po = pkglist_begin(pl) ; po ; po = pkglist_next(po)){
			printf("%s %s\n",pkgobj_name(po),pkgobj_version(po));
			if(augment_dfa(&dfa,pkgobj_name(po),main)){
				fprintf(stderr,"Error augmenting DFA (%s)\n",pkgobj_name(po));
				return EXIT_FAILURE;
			}
			++pkgs;
		}
	}
	if(pkgs != pkgcache_count(pc)){
		fprintf(stderr,"Package count was inaccurate (%u != %u)\n",
				pkgs,pkgcache_count(pc));
		return EXIT_FAILURE;
	}
	free_package_cache(pc);
	free_dfa(dfa);

	dfa = NULL;
	if((pc = pkgcache_from_pkglist(lex_packages_file(argv[1],&err,&dfa),&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",argv[1],strerror(err));
		return EXIT_FAILURE;
	}
	pkgs = 0;
	printf("Reported package count: %u\n",pkgcache_count(pc));
	for(pl = pkgcache_begin(pc) ; pl ; pl = pkgcache_next(pl)){
		for(po = pkglist_begin(pl) ; po ; po = pkglist_next(po)){
			printf("%s %s\n",pkgobj_name(po),pkgobj_version(po));
			++pkgs;
		}
	}
	if(pkgs != pkgcache_count(pc)){
		fprintf(stderr,"Package count was inaccurate (%u != %u)\n",
				pkgs,pkgcache_count(pc));
		return EXIT_FAILURE;
	}
	free_package_cache(pc);
	free_dfa(dfa);

	printf("Successfully parsed %s (%u package%s)\n",argv[1],
			pkgs,pkgs == 1 ? "" : "s");
	return EXIT_SUCCESS;
}
