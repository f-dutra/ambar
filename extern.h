typedef struct {
	char *name;
	int x, w;
	char *cmd;
	char *text;
	int interval;
	int sig;
	int pos;
	int dirty;
} Module;

enum { LEFT, CENTER, RIGHT };

extern Display *dpy;

