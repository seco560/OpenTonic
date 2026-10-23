/* Small path and string helpers. Results are SDL_malloc'd; free with SDL_free. */
#ifndef TV_UTIL_H
#define TV_UTIL_H
#include <stdbool.h>

char *path_join(const char *a, const char *b);  /* a/b (b as-is when a is empty) */
char *path_dir(const char *path);               /* everything before the last '/', "." when none */
char *path_stem(const char *path);              /* file name without folder and extension */
char *path_with_ext(const char *path, const char *ext);  /* replaces the extension, ext like ".ttai" */
bool path_is_file(const char *path);
bool path_is_dir(const char *path);

#endif
