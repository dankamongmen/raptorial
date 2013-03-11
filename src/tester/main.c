#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <raptorial.h>

static void
usage(const char *name){
	fprintf(stderr,"usage: %s packagesfile\n",name);
}

int main(int argc,char **argv){
	struct pkgcache *pc;
	struct pkglist *pl;
	struct pkgobj *po;
	unsigned pkgs;
	int err;

	if(argc != 2){
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if((pc = pkgcache_from_pkglist(parse_packages_file(argv[1],&err),&err)) == NULL){
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
	printf("Successfully parsed %s (%u package%s)\n",argv[1],
			pkgs,pkgs == 1 ? "" : "s");
	return EXIT_SUCCESS;
}
