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
	int err;

	if(argc != 2){
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if((pc = parse_packages_file(argv[1],&err)) == NULL){
		fprintf(stderr,"Couldn't parse %s (%s?)\n",argv[1],strerror(err));
		return EXIT_FAILURE;
	}
	free_package_cache(pc);
	printf("Successfully parsed %s\n",argv[1]);
	return EXIT_SUCCESS;
}
