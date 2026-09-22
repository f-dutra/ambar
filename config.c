#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>

#include "util.h"
#include "extern.h"
#include "config.h"

#define GRAY		"#ff555555"
#define GRAYDARK	"#ff222222"
#define WHITE		"#ffe0e0e0"
#define WHITEALT	"#ffffffff"
#define RED		"#fff1212c"
#define REDALT		"#fff1414c"
#define BLUE		"#ff818cf1"

#define CLRLEN 10
#define STRLEN 128
#define CONFNAME "ambar.conf"

enum { NOTHING, INTEGER, DOUBLE, STRING, SPAWN };

typedef struct {
	char *name;
	int type;
	void *dst;
	size_t size;
} Parser;

static void addmodule(Module mod);
static void cfg_reset(void);
static int findpath(char *out, size_t outsz);
static void loadresource(XrmDatabase db, Parser *p);
static char *nextfield(char **p);
static void parsemodule(char *value);
static size_t parsersize(Parser *p);
static void parsevar(char *name, char *value);
static void savedefaults(void);
static char *trim(char *s);
static char *unquote(char *s);

int borderpx 		       = 1;	/* frame border */
int moduleradius = 13;
char fontname[STRLEN] 	       = "monospace";	/* font used on window titles */
double fontsize 	       = 16.00;
int margin			  = 8;	/* titlebar padding */
int marginvert			  = 8;	/* titlebar padding */
int padding			  = 6;	/* titlebar padding */
int lrpad		 	       = 8; /* padding between buttons and text */
int offset_y			  = 1;  /* offset elements downwards */
int height			  = 32;

/* Colors */
/* The WM supports both rgb and argb hex colors */
char bgnorm[CLRLEN]		       = "#ff000000"; /* norm means unfocused window */
char bgsel[CLRLEN]	     	  = "#ff2a2a2a"; /* sel means focused window */
char bghover[CLRLEN]	     	  = "#ff2a2a2a"; /* sel means focused window */

char fgnorm[CLRLEN]    		  = WHITE;
char fgsel[CLRLEN]     		  = WHITE;
char fghover[CLRLEN]     		  = WHITE;

char wsbgnorm[CLRLEN] = "#121212";
char wsfgnorm[CLRLEN] = GRAY;
char wsfgsel[CLRLEN]  = WHITE;

char bordernorm[CLRLEN]		  = "#ff555555";
char bordersel[CLRLEN]		  = "#ff6a6a6a";
char borderhover[CLRLEN]		  = "#ff6a6a6a";

int workspacelrpad = 8;
int workspacepadding = 16;
int workspacepx = 8;
int workspaceselw = 34;
int workspaceselh = 10;

Module *modules;
int nmodules = 0;

/* used to parse the config */
Parser config[] = {
	{ "fontname", 		     STRING,  fontname, STRLEN },
	{ "fontsize", 			DOUBLE,  &fontsize, 0 },
	{ "offset_y", 			INTEGER, &offset_y, 0 },
	{ "margin", 			INTEGER, &margin, 0 },
	{ "margin-vertical", 	INTEGER, &marginvert, 0 },
	{ "workspacepx", 		INTEGER, &workspacepx, 0 },
	{ "workspaceselh", 		INTEGER, &workspaceselh, 0 },
	{ "workspaceselw", 		INTEGER, &workspaceselw, 0 },
	{ "workspace-lrpad", 	INTEGER, &workspacelrpad, 0 },
	{ "workspace-padding", 	INTEGER, &workspacepadding, 0 },
	{ "lrpad", 			INTEGER, &lrpad, 0 },
	{ "module-radius", 		INTEGER, &moduleradius, 0 },
	{ "height", 			INTEGER, &height, 0 },
	{ "padding", 			INTEGER, &padding, 0 },
	{ "bgnorm", 			STRING,  bgnorm, CLRLEN },
	{ "bgsel", 			STRING,  bgsel, CLRLEN },
	{ "bghover", 			STRING,  bghover, CLRLEN },
	{ "fgnorm", 			STRING,  fgnorm, CLRLEN },
	{ "fgsel", 			STRING,  fgsel, CLRLEN },
	{ "wsbgnorm", 			STRING,  wsbgnorm, CLRLEN },
	{ "wsfgnorm", 			STRING,  wsfgnorm, CLRLEN },
	{ "wsfgsel", 			STRING,  wsfgsel, CLRLEN },
	{ "fghover", 			STRING,  fghover, CLRLEN },
	{ "bordernorm", 		STRING,  bordernorm, CLRLEN },
	{ "bordersel", 		STRING,  bordersel, CLRLEN },
};

static int watchfd = -1;
static void *defaults[LENGTH(config)];

void
addmodule(Module mod)
{
	Module *tmp;

	if (!(tmp = realloc(modules, (nmodules + 1) * sizeof(Module))))
		die("realoc: ");
	modules = tmp;
	modules[nmodules++] = mod;
}

void
cfg_cleanup(void)
{
	int i;

	for (i = 0; i < nmodules; i++) {
		free(modules[i].text);
		free(modules[i].cmd);
	}
	free(modules);
	modules = NULL;
	nmodules = 0;
}

void
cfg_load(void)
{
	FILE *file;
	char path[512];
	char line[2048];
	char *eq, *trimmed, *name, *value;

	savedefaults();
	if(!(findpath(path, sizeof(path))))
		return;
	if (!(file = fopen(path, "r")))
		return;

	while (fgets(line, sizeof(line), file)) {
		trimmed = trim(line);
		if (*trimmed == '\0' || *trimmed == '#')
			continue;
		if (!(eq = strchr(trimmed, '=')))
			continue;

		*eq = '\0';
		name = trim(trimmed);
		value = trim(eq + 1);
		if (strcmp(name, "module") == 0)
			parsemodule(value);
		else
			parsevar(name, value);
	}
	fclose(file);
}

void
cfg_load_xresources(void)
{
	char *resm;
	XrmDatabase db;
	Parser *p;

	resm = XResourceManagerString(dpy);
	if (!resm)
		return;

	db = XrmGetStringDatabase(resm);
	for (p = config; p < config + LENGTH(config); p++)
		loadresource(db, p);
	XrmDestroyDatabase(db);
}

void
cfg_reload(void)
{
	cfg_reset();
	cfg_load();
	cfg_load_xresources();
}

void
cfg_reset(void)
{
	size_t i;

	for (i = 0; i < LENGTH(config); i++)
		if (defaults[i])
			memcpy(config[i].dst, defaults[i], parsersize(&config[i]));
	cfg_cleanup();
}

void
cfg_watch_cleanup(void)
{
	if (watchfd >= 0)
		close(watchfd);
	watchfd = -1;
}

int
cfg_watch_getfd(void)
{
	return watchfd;
}

int
cfg_watch_handle(void)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	const struct inotify_event *ev;
	ssize_t len;
	char *p;
	int changed = 0;

	if (watchfd < 0)
		return 0;
	while ((len = read(watchfd, buf, sizeof(buf))) > 0)
		for (p = buf; p < buf + len; p += sizeof(*ev) + ev->len) {
			ev = (const struct inotify_event *)p;
			if (ev->len && strcmp(ev->name, CONFNAME) == 0)
				changed = 1;
		}
	return changed;
}

void
cfg_watch_init(void)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char dir[512];
	uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO;
	int ok = 0;

	if ((watchfd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0)
		return;
	/* watch every directory findpath() might pick a file from */
	if (xdg && *xdg) {
		snprintf(dir, sizeof(dir), "%s/ambar", xdg);
		ok |= inotify_add_watch(watchfd, dir, mask) >= 0;
	}
	if (home && *home) {
		snprintf(dir, sizeof(dir), "%s/.config/ambar", home);
		ok |= inotify_add_watch(watchfd, dir, mask) >= 0;
	}
	if (!ok) {
		close(watchfd);
		watchfd = -1;
	}
}

int
findpath(char *out, size_t outsz)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (xdg && *xdg) {
		snprintf(out, outsz, "%s/ambar/ambar.conf", xdg);
		if (access(out, R_OK) == 0)
			return 1;
	}
	if (home && *home) {
		snprintf(out, outsz, "%s/.config/ambar/ambar.conf", home);
		if (access(out, R_OK) == 0)
			return 1;
	}

	snprintf(out, outsz, "/etc/ambar/ambar.conf");
	if (access(out, R_OK) == 0)
		return 1;

	return 0;
}

void
loadresource(XrmDatabase db, Parser *p)
{
	char fullname[256];
	char *type;
	XrmValue ret;

	snprintf(fullname, sizeof(fullname), "%s.%s", "ambar", p->name);
	fullname[sizeof(fullname) - 1] = '\0';

	if (!XrmGetResource(db, fullname, "*", &type, &ret))
		return;
	if (strcmp(type, "String") != 0)
		return;

	if (p->type == STRING)
		snprintf((char *)p->dst, p->size, "%s", ret.addr);
	else if (p->type == INTEGER)
          parseint(ret.addr, p->dst);
	else if (p->type == DOUBLE)
		parsedouble(ret.addr, p->dst);
}

char *
nextfield(char **p)
{
    char *field;

    if (!p || !*p)
        return NULL;

    field = strsep(p, ",");
    return trim(field);
}

void
parsemodule(char *value)
{
	char buf[2048];
	char *open, *close, *p;
	char *namestr, *cmdstr, *intervalstr;
	char *sigstr, *posstr;
	Module mod = {0};

	strncpy(buf, value, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (!(open = strchr(buf, '{')) || !(close = strrchr(buf, '}')))
		return;

	*close = '\0';
	p = open + 1;

	namestr = nextfield(&p);
	cmdstr = nextfield(&p);
	intervalstr = nextfield(&p);
	sigstr = nextfield(&p);
	posstr = nextfield(&p);

	if (!namestr || !cmdstr || !intervalstr || !sigstr || !posstr)
		return;
	if (!*namestr || !*cmdstr || !*intervalstr || !*sigstr || !*posstr)
		return;
	if (!parseint(intervalstr, &mod.interval))
		return;
	if (!parseint(sigstr, &mod.sig))
		return;
	if (strcmp(posstr, "left") == 0)
		mod.pos = LEFT;
	else if (strcmp(posstr, "center") == 0)
		mod.pos = CENTER;
	else if (strcmp(posstr, "right") == 0)
		mod.pos = RIGHT;
	else {
		fprintf(stderr, "parsemodule: bad pos '%s'\n", posstr); fflush(stderr);
		return;
	}
	mod.name = strdup(unquote(namestr));
	mod.cmd = strdup(unquote(cmdstr));
	mod.text = ecalloc(1, 50);

	if (!mod.name || !mod.cmd) {
		free(mod.name);
		free(mod.cmd);
		return;
	}
	addmodule(mod);
}

size_t
parsersize(Parser *p)
{
	switch (p->type) {
	case INTEGER:
		return sizeof(int);
	case DOUBLE:
		return sizeof(double);
	case STRING:
		return p->size;
	}
	return 0;
}

void
parsevar(char *name, char *value)
{
	for (size_t i = 0; i < LENGTH(config); i++) {
		if (strcmp(name, config[i].name) != 0)
			continue;
          if (config[i].type == INTEGER) {
               parseint(value, (int *)config[i].dst);
		} else if (config[i].type == DOUBLE) {
			parsedouble(value, (double *)config[i].dst);
		} else if (config[i].type == STRING) {
			snprintf((char *)config[i].dst, config[i].size, "%s", unquote(value));
		}
		break;
	}
}

void
savedefaults(void)
{
	size_t i, sz;

	if (defaults[0])
		return;
	for (i = 0; i < LENGTH(config); i++) {
		sz = parsersize(&config[i]);
		defaults[i] = ecalloc(1, sz);
		memcpy(defaults[i], config[i].dst, sz);
	}
}

char
*trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0)
	    return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

char *
unquote(char *s)
{
	size_t len = strlen(s);

	if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
		s[len - 1] = '\0';
		s++;
	}
	return s;
}

