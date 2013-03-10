#include <raptorial.h>

#define STATUSFILE_DEFAULT "/var/lib/dpkg/status"
#define LISTDIR_DEFAULT "/var/lib/apt/lists"

const char *raptorial_def_lists_dir(void){
	return LISTDIR_DEFAULT;
}

const char *raptorial_def_status_file(void){
	return STATUSFILE_DEFAULT;
}
