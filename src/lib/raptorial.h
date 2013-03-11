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
      #define PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define PUBLIC __attribute__ ((dllimport))
    #else
      #define PUBLIC __declspec(dllimport)
    #endif
  #endif
#else
  #if __GNUC__ >= 4
    #define PUBLIC __attribute__ ((visibility ("default")))
  #else
    #define PUBLIC
  #endif
#endif

#include <stddef.h>

struct pkgobj;
struct pkglist;
struct pkgcache;

// Returns a new package cache object after parsing the specified package list.
// On error, NULL is returned, and the error value will be written through; it
// is otherwise untouched.
PUBLIC struct pkglist *
parse_packages_file(const char *,int *);

// Returns a new package cache object after parsing any package lists found in
// the specified directory.
PUBLIC struct pkgcache *
parse_packages_dir(const char *,int *);

// Returns a new package cache object after parsing the provided package list.
// On error, NULL is returned, and the error value will be written through; it
// is otherwise untouched.
PUBLIC struct pkglist *
parse_packages_mem(const void *,size_t,int *);

// Wrap a package list in a single-index cache object. Returns NULL if passed
// NULL, without modifying err, allowing use in functional composition. Frees
// the pkglist on its own internal error, returning NULL and setting err.
PUBLIC struct pkgcache *
pkgcache_from_pkglist(struct pkglist *,int *);

PUBLIC void free_package_list(struct pkglist *);

// Free the pkgcache and any associated state, including pkglists therein.
PUBLIC void free_package_cache(struct pkgcache *);

PUBLIC struct pkglist *
pkgcache_begin(struct pkgcache *);

PUBLIC struct pkglist *
pkgcache_next(struct pkglist *);

PUBLIC struct pkgobj *
pkglist_begin(struct pkglist *);

PUBLIC struct pkgobj *
pkglist_next(struct pkgobj *);

PUBLIC const struct pkgobj *
pkglist_cbegin(const struct pkglist *);

PUBLIC const struct pkgobj *
pkglist_cnext(const struct pkgobj *);

PUBLIC const char *
pkgobj_name(const struct pkgobj *);

PUBLIC const char *
pkglist_dist(const struct pkglist *);

PUBLIC const char *
pkgobj_version(const struct pkgobj *);

PUBLIC unsigned
pkgcache_count(const struct pkgcache *);

PUBLIC const char *
raptorial_def_lists_dir(void);

PUBLIC const char *
raptorial_def_status_file(void);

#ifdef __cplusplus
}
#endif

#endif
