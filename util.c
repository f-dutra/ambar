/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void
die(const char *fmt, ...)
{
	va_list ap;
	int saved_errno;

	saved_errno = errno;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);

	exit(1);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

void
utf8truncate(char *s)
{
	int len = strlen(s), seqlen;
	int i = len;

	while (i > 0 && (s[i - 1] & 0xC0) == 0x80)
		i--;
	if (i == 0)
		return;
	i--;

	unsigned char lead = (unsigned char)s[i];
	if ((lead & 0x80) == 0x00) {
		seqlen = 1; /* ASCII */
	} else if ((lead & 0xE0) == 0xC0) {
		seqlen = 2;
	} else if ((lead & 0xF0) == 0xE0) {
		seqlen = 3;
	} else if ((lead & 0xF8) == 0xF0) {
		seqlen = 4;
	} else {
		s[i] = '\0';
		return;
	}

	if (i + seqlen > len)
		s[i] = '\0';
}

