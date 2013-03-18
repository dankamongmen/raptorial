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

struct dfa;
struct dfactx;
struct pkgobj;
struct pkglist;
struct pkgcache;

// Returns a new package list object after lexing the specified package list.
// On error, NULL is returned, and the error value will be written through; it
// is otherwise untouched. The package list will be broken into chunks, and
// lexed in parallel.
//
// If dfa is NULL, it will be unused. If it points to a NULL, we will build it.
// If it points to a non-null, we will filter our list based on it.
PUBLIC struct pkglist *
lex_packages_file(const char *,int *,struct dfa **);

// Returns a new package list object after lexing the provided package list.
// On error, NULL is returned, and the error value will be written through; it
// is otherwise untouched. The package list will be broken into chunks, and
// lexed in parallel.
//
// If dfa is NULL, it will be unused. If it points to a NULL, we will build it.
// If it points to a non-null, we will filter our list based on it.
PUBLIC struct pkglist *
lex_packages_mem(const void *,size_t,int *,struct dfa **);

// Returns a new package cache object after lexing any package lists found in
// the specified directory. The lists will be processed in parallel.
//
// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA, since it lexes file chunks in parallel.
PUBLIC struct pkgcache *
lex_packages_dir(const char *,int *,struct dfa *);

// Wrap a package list in a single-index cache object. Returns NULL if passed
// NULL, without modifying err, allowing use in functional composition. Frees
// the pkglist on its own internal error, returning NULL and setting err.
PUBLIC struct pkgcache *
pkgcache_from_pkglist(struct pkglist *,int *);

PUBLIC void free_package_list(struct pkglist *);

// Free the pkgcache and any associated state, including pkglists therein.
PUBLIC void free_package_cache(struct pkgcache *);

// Lex a dpkg status file. On error, NULL is returned and the error value is
// written through. On success, it is not touched. The status file will be
// broken into chunks, and lexed in parallel.
//
// If dfa is NULL, it will be unused. If it points to a NULL, we will build it.
// If it points to a non-null, we will filter our list based on it.
PUBLIC struct pkglist *
lex_status_file(const char *,int *,struct dfa **);

PUBLIC const struct pkglist *
pkgcache_begin(const struct pkgcache *);

PUBLIC const struct pkglist *
pkgcache_next(const struct pkglist *);

PUBLIC const struct pkgobj *
pkglist_begin(const struct pkglist *);

PUBLIC const struct pkgobj *
pkglist_next(const struct pkgobj *);

PUBLIC const struct pkgobj *
pkglist_find(const struct pkglist *,const char *);

PUBLIC const struct pkgobj *
pkgcache_find_newest(const struct pkgcache *,const char *,const struct pkglist **);

PUBLIC const char *
pkgobj_name(const struct pkgobj *);

PUBLIC const char *
pkglist_uri(const struct pkglist *);

PUBLIC const char *
pkglist_dist(const struct pkglist *);

// Returns a status string when applied to a pkgobj from the status pkglist.
// Otherwise, returns NULL.
PUBLIC const char *
pkgobj_status(const struct pkgobj *);

PUBLIC const char *
pkgobj_version(const struct pkgobj *);

PUBLIC unsigned
pkgcache_count(const struct pkgcache *);

PUBLIC const char *
raptorial_def_lists_dir(void);

PUBLIC const char *
raptorial_def_status_file(void);

typedef struct dfactx {
	const struct dfa *dfa;
	const struct dfavtx *cur;
} dfactx;

dfactx *create_dfactx(const struct dfa *);

void init_dfactx(dfactx *,const struct dfa *);

PUBLIC int
augment_dfa(struct dfa **,const char *,void *);

PUBLIC void
free_dfa(struct dfa *);

PUBLIC void *
match_dfactx_string(struct dfactx *,const char *);

PUBLIC int
walk_dfa(const struct dfa *,int (*)(const char *,const void *),const void *);

#ifdef __cplusplus
}
#endif

#endif
