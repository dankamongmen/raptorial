#include <stdlib.h>
#include <raptorial.h>

int main(void){
	char *pcpath = NULL;
	struct pkgcache *pc;
	int err;

	if((pc = parse_packages_file(pcpath,&err)) == NULL){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
