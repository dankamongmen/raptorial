#ifndef LIBRAPTORIAL_RAPTORIAL
#define LIBRAPTORIAL_RAPTORIAL

#ifdef __cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DLL
    #ifdef __GNUC__
      #define PUBLIC __attribute__ ((dllexport))
    #else
      #define PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define PUBLIC __attribute__ ((dllimport))
    #else
      #define PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
#else
  #if __GNUC__ >= 4
    #define PUBLIC __attribute__ ((visibility ("default")))
  #else
    #define PUBLIC
  #endif
#endif

struct pkgcache;

// Returns a new package cache object after parsing the specified
// package list. On error, the error value will be written through;
// this object is otherwise untouched.
PUBLIC struct pkgcache *
parse_packages_file(const char *,int *);

// Returns a new package cache object after parsing the provided
// package list. On error, the error value will be written through;
// this object is otherwise untouched.
PUBLIC struct pkgcache *
parse_packages_mem(const void *,size_t,int *);

PUBLIC void
free_package_cache(struct pkgcache *);

#ifdef __cplusplus
}
#endif

#endif
