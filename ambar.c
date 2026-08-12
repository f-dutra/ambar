#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xft/Xft.h>
#include <X11/Xresource.h>

#include "drw.h"
#include "util.h"

enum { SchemeNorm, SchemeSel, SchemeLast };
enum { NetActiveWindow, NetClientList, NetClientListStacking,
	  NetCurrentDesktop, NetWMDesktop, NetWMName, NetWMIcon,
	  NetWMState, NetWMWindowType, NetWMWindowTypeDock,
	  NetWMStrut, NetWMStrutPartial, NetNumberOfDesktops,
	  NetDesktopNames, NetLast };

typedef struct {
	int n;
	int occ;
	char name[256];
} Workspace;

static void cleanup(void);
static void draw(void);
static unsigned long *getprop(Window w, Atom prop, Atom type, unsigned long *nitems);
static void run(void);
static void setup(void);
static void setupwindow(void);
static void updatecurrentdesktop(void);
static void updatedesktopnames(void);
static void updatedesktops(void);
static void updatestrut(void);

static int running = 1;
static Display *dpy;
static Window root;
static Window barwin;
static int screen, sw, sh;
static int mx, my, mw, mh;
static int winx, winy, winw, winh;
static Surf *srf;

static Colormap cmap;
static int depth;
static Visual *visual;
Fnt *fnt;

static int currentdesktop = -1;
static int ndesktop = 0;
static int desktopnamelen = 0;
static char *desktopnames;

static Atom netatom[NetLast];
static Atom wmstate, utf8string;
static double scheme[SchemeLast][3][4];

#include "config.h"

void
draw(void)
{
	int i;
	int x = margin;
	char *p;
	Drw *drw = drw_create(srf);

	drw_set_scheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, winw, winh, 0, 1, 0);

	drw_set_font(drw, fnt);
	p = desktopnames;

	while (desktopnames && p < desktopnames + desktopnamelen) {
		drw_text(drw, p, x + lrpad / 2, margin);
		x += TEXTW(fnt, p) + lrpad;
		p += strlen(p) + 1;
	}


	drw_destroy(drw);
}

unsigned long *
getprop(Window w, Atom prop, Atom type, unsigned long *nitems)
{
	int format;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da = None;

	if (XGetWindowProperty(dpy, w, prop, 0L, 0x7fffffff, False, AnyPropertyType,
		&da, &format, nitems, &dl, &p) == Success && p) {
		if (!p || nitems == 0) {
			XFree(p);
			return NULL;
		}
	}
	return (unsigned long *)p;
}

void
run(void)
{
	XEvent ev;
	draw();

	while (running && !XNextEvent(dpy, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw();
			break;
		}
	}
}

void
setup(void)
{
	XVisualInfo vinfo;
	int i, j;

	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);

	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	netatom[NetWMStrut] = XInternAtom(dpy, "_NET_WM_STRUT", False);
	netatom[NetWMStrutPartial] = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);

	if (XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo)) {
		visual = vinfo.visual;
		depth  = vinfo.depth;
		cmap   = XCreateColormap(dpy, root, visual, AllocNone);
	} else {
		die("couldn't allocate visual");
	}

	for (i = 0; i < LENGTH(colors); i++)
		for (j = 0; j < LENGTH(colors[i]); j++)
			drw_color_create(colors[i][j], scheme[i][j]);

	fnt = drw_font_create(fontname, fontsize);

	updatecurrentdesktop();
	updatedesktopnames();
	updatedesktops();
	setupwindow();
}

void
setupwindow(void)
{
	XSetWindowAttributes wa;
	XClassHint ch = { "ambar", "ambar" };

	wa.background_pixel = 0xff000000;
	wa.border_pixel = 0;
	wa.colormap = cmap;
	wa.backing_store = WhenMapped; /* cache the pixels */
	wa.event_mask = ExposureMask|ButtonPressMask|PointerMotionMask|EnterWindowMask;

	winx = 0;
	winy = 0;
	winw = sw;
	winh = fnt->h + 2 * margin;

	barwin = XCreateWindow(dpy, root, winx, winy,
				        (unsigned)winw, (unsigned)winh, 0, depth,
					   InputOutput, visual, CWBackPixel|CWBorderPixel|
					   CWEventMask|CWColormap|CWBackingStore, &wa);
	XSetClassHint(dpy, barwin, &ch);
	XChangeProperty(dpy, barwin, netatom[NetWMWindowType], XA_ATOM, 32,
	PropModeReplace, (unsigned char *)&netatom[NetWMWindowTypeDock], 1);
	srf = drw_surf_create(dpy, barwin, visual, winw, winh);

	updatestrut();
	XMapRaised(dpy, barwin);
}

void
updatecurrentdesktop(void)
{
	unsigned long n = 0;
	unsigned long *prop;

	if ((prop = getprop(root, netatom[NetCurrentDesktop], XA_CARDINAL, &n)) != NULL)
		currentdesktop = prop[0];
	else
		currentdesktop = -1;
	XFree(prop);
}

void
updatedesktopnames(void)
{
	unsigned long n = 0;
	unsigned long *prop;

	free(desktopnames);
	desktopnames = NULL;
	desktopnamelen = 0;

	if ((prop = getprop(root, netatom[NetDesktopNames], utf8string, &n)) != NULL) {
		desktopnames = ecalloc(1, n);
		memcpy(desktopnames, prop, n);
		desktopnamelen = n;
		XFree(prop);
	}
}

void
updatedesktops(void)
{
	unsigned long n = 0;
	unsigned long *prop;

	if ((prop = getprop(root, netatom[NetNumberOfDesktops], XA_CARDINAL, &n)) != NULL)
		ndesktop = prop[0];
	else
		ndesktop = 0;
	XFree(prop);
}

void
updatestrut(void)
{
	long strutpartial[12] = { 0 };
	long strut[4] = { 0 };

	strutpartial[2] = winh;
	strutpartial[8] = winx;
	strutpartial[9] = winx + winw - 1;

	strut[2] = winh;

	XChangeProperty(dpy, barwin, netatom[NetWMStrutPartial], XA_CARDINAL, 32,
	PropModeReplace, (unsigned char *)strutpartial, 12);
	XChangeProperty(dpy, barwin, netatom[NetWMStrut], XA_CARDINAL, 32,
	PropModeReplace, (unsigned char *)strut, 4);
}

int
main(int argc, char **argv)
{
	if (!(dpy = XOpenDisplay(NULL)))
     	die("ambar;: cannot open display");
	setup();
	run();
	XCloseDisplay(dpy);
}

