
// If dfa is non-NULL, it will be used to filter our list. This function is
// not capable of building a DFA.
PUBLIC struct pkgcache *
lex_packages_dir(const char *,int *,struct dfa *);
