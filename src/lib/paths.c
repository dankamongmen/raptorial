#include <raptorial.h>

#define LISTDIR_DEFAULT "/var/lib/apt/lists"
#define STATUSFILE_DEFAULT "/var/lib/dpkg/status"
#define CONTENTDIR_DEFAULT "/var/cache/apt/apt-file"

const char *raptorial_def_lists_dir(void){
	return LISTDIR_DEFAULT;
}

const char *raptorial_def_status_file(void){
	return STATUSFILE_DEFAULT;
}

const char *raptorial_def_content_dir(void){
	return CONTENTDIR_DEFAULT;
}
