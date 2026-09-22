/* See LICENSE file for copyright and license details. */

#ifndef MAX	/* gmacros, pulled by pango already defines those */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#endif

#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))
#define LENGTH(X)               (sizeof (X) / sizeof (X)[0])

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
void utf8truncate(char *s);
int parsedouble(const char *s, double *out);
int parseint(const char *s, int *out);

