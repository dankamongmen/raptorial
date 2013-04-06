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
struct changelog;

// Returns a new package list object after lexing the specified package list.
// On error, NULL is returned, and the error value will be written through; it
// is otherwise untouched. The package list will be broken into chunks, and
// lexed in parallel.
//
// If dfa is NULL, it will be unused. If it points to a NULL, we will build it.
// If it points to a non-null, we will filter our list based on it.
PUBLIC struct pkglist *
lex_packages_file(const char *,int *,struct dfa **);

// Returns a new package cache object after lexing any package lists found in
// the specified directory. The lists will be processed in parallel.
//
// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA.
PUBLIC struct pkgcache *
lex_packages_dir(const char *,int *,struct dfa *);

// Walks all contents cachefiles. The tables will be processed in parallel.
// Set the nocase flag for insensitive matching; in this case, the DFA must
// have been built using lowercased patterns.
//
// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA.
PUBLIC int
lex_contents_dir(const char *,int *,struct dfa *,int nocase);

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
pkgcache_find_newest(const struct pkgobj *);

PUBLIC const struct pkgobj *
pkgcache_find_installed(const struct pkgobj *);

PUBLIC const char *
pkgobj_name(const struct pkgobj *);

PUBLIC const char *
pkglist_uri(const struct pkglist *);

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

PUBLIC const char *
raptorial_def_content_dir(void);

PUBLIC const char *
raptorial_def_changelog(void);

typedef struct dfactx {
	const struct dfa *dfa;
	unsigned cur;
} dfactx;

dfactx *create_dfactx(const struct dfa *);

PUBLIC struct pkgobj *
create_stub_package(const char *,int *);

void init_dfactx(dfactx *,const struct dfa *);

PUBLIC int
augment_dfa(struct dfa **,const char *,void *);

PUBLIC void
free_dfa(struct dfa *);

PUBLIC void *
match_dfactx_string(struct dfactx *,const char *);

PUBLIC int
walk_dfa(const struct dfa *,int (*)(const char *,const void *,const void *),
					const void *);

PUBLIC const struct pkgobj *
pkgobj_matchbegin(const struct pkgobj *);

PUBLIC const struct pkgobj *
pkgobj_matchnext(const struct pkgobj *);

PUBLIC const char *
pkgobj_uri(const struct pkgobj *);

PUBLIC const char *
pkgobj_dist(const struct pkgobj *);

PUBLIC int
debcmp(const char *,const char *);

// In the case of an error, you can still get the lexed
// stack of changelog entries through the value-result.
PUBLIC const struct changelog *
lex_changelog(const char *,int *,const struct changelog **);

PUBLIC const char *
changelog_getsource(const struct changelog *);

PUBLIC const char *
changelog_getversion(const struct changelog *);

PUBLIC const char *
changelog_getdist(const struct changelog *);

PUBLIC const char *
changelog_geturg(const struct changelog *);

PUBLIC const char *
changelog_getmaintainer(const struct changelog *);

PUBLIC const char *
changelog_getdate(const struct changelog *);

PUBLIC const char *
changelog_getchanges(const struct changelog *);

PUBLIC const struct changelog *
changelog_getnext(const struct changelog *);

#ifdef __cplusplus
}
#endif

#endif
