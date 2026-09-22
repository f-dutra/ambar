#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xft/Xft.h>
#include <X11/Xresource.h>

#include "drw.h"
#include "util.h"
#include "extern.h"
#include "config.h"

#define CMDLENGTH 50

enum { SchemeNorm, SchemeSel, SchemeWorkspace, SchemeWsNorm,
	  SchemeWsSel, SchemeLast };
enum { NetActiveWindow, NetClientList, NetClientListStacking,
	  NetCurrentDesktop, NetWMDesktop, NetWMName, NetWMIcon,
	  NetWMState, NetWMWindowType, NetWMWindowTypeDock,
	  NetWMStrut, NetWMStrutPartial, NetNumberOfDesktops,
	  NetDesktopNames, NetLast };

typedef struct {
	int n;
	int occ;
} Workspace;

static void replace(char *str, char old, char new);
static void remove_all(char *str, char to_remove);
static void buttonpress(XEvent *e);
static void cleanup(void);
static void draw(void);
static int draw_workspaces(Drw *drw, Module *mod, int limit, int reverse, int measure);
static int draw_module(Drw *drw, Module *mod, int limit, int reverse);
static void getcmd(Module *mod);
static void getcmds(int time);
static void getsigcmds(int signal);
static unsigned long *getprop(Window w, Atom prop, Atom type, unsigned long *nitems);
static void propertynotify(XEvent *e);
static void redraw_module(Module *mod);
static void redraw_region(int pos);
static void redraw_workspaces(int checkreg);
static void reloadconfig(void);
static void run(void);
static void setup(void);
static void setupwindow(void);
static void sighandler(void);
static void sighup(int unused);
static void sigterm(int unused);
static void updatedesktops(void);
static void updatestrut(void);
static void updateworkspaces(void);
static void view(long workspace);

static int running = 1;
static int restart = 0;
Display *dpy;
static Window root;
static Window barwin;
static int screen, sw, sh;
static int mx, my, mw, mh;
static int winx, winy, winw, winh;
static Surf *srf;

static Colormap cmap;
static int depth;
static Visual *visual;

static int signalFD;
static int timerInterval = -1;

static Fnt *font;
static Workspace *workspaces;

static int currentdesktop = -1;
static int ndesktop = 0;
static int desktoppendingconfirm = 0;

static int regx[3] = {0};
static int regw[3] = {0};
static int regionbox[3][2];

static Atom netatom[NetLast];
static Atom wmstate, utf8string;
static double scheme[SchemeLast][3][4];

static char *colors[SchemeLast][3] = {
	[SchemeNorm] = { fgnorm, bgnorm, bordernorm },
	[SchemeSel]  = { fgsel, bgsel, bordersel  },
	[SchemeWorkspace]  = { fgsel, wsbgnorm, bordersel  },
	[SchemeWsNorm] = { fgnorm, wsfgnorm, bordernorm },
	[SchemeWsSel]  = { fgsel, wsfgsel, bordersel  },
};

void
buttonpress(XEvent *e)
{
	XButtonPressedEvent *ev = &e->xbutton;
	static Time lasttime = 0;
	Module *mod = NULL;
	int i, x;

	if (ev->window != barwin)
		return;
	for (i = 0; i < nmodules; i++)
		if (ev->x >= modules[i].x && ev->x < modules[i].x + modules[i].w)
			mod = &modules[i];
	if (!mod)
		return;

	if (strcmp(mod->name, "workspaces") == 0) {
		if ((ev->time - lasttime) <= 30)
			return;
		lasttime = ev->time;
		i = 0;
		x = mod->x + padding / 2;
		do {
			x += (i == currentdesktop ? workspaceselw: workspacepx) + workspacelrpad;
		} while (ev->x >= x && ++i < ndesktop);

		if (i < ndesktop) {
			if (ev->button == Button1)
				view(i);
			else if (ev->button == Button5)
				view(currentdesktop < ndesktop - 1 ? currentdesktop + 1 : ndesktop - 1);
			else if (ev->button == Button4)
				view(currentdesktop > 0 ? currentdesktop - 1 : 0);
		}
	} else if (mod->sig != 0) {
		char button[2] = {'0' + ev->button & 0xff, '\0'};
		pid_t process_id = getpid();
		if (fork() == 0) {
			char shcmd[1024];
			sprintf(shcmd,"%s && kill -%d %d", mod->cmd, mod->sig+SIGRTMIN, process_id);
			char *command[] = { "/bin/sh", "-c", shcmd, NULL };
			setenv("BLOCK_BUTTON", button, 1);
			setsid();
			execvp(command[0], command);
			exit(EXIT_SUCCESS);
		}
	}
}

int
computelayout(int pos)
{
	int i, x, w, changed = 0;

	for (i = 0; i < nmodules; i++) {
		if (modules[i].pos != pos) continue;
		if (strcmp(modules[i].name, "workspaces") == 0)
			w = draw_workspaces(NULL, NULL, winw, 0, 1) + workspacepadding;
		else
			w = TEXTW(modules[i].text) + padding;
		if (w != modules[i].w) changed = 1;
		modules[i].w = w;
	}
	if (!changed)
		return 0;

	regw[pos] = 0;
	for (i = 0; i < nmodules; i++)
		if (modules[i].pos == pos)
			regw[pos] += modules[i].w + lrpad;

	if (pos == LEFT)        regx[LEFT] = margin;
	else if (pos == CENTER) regx[CENTER] = (winw - regw[CENTER]) / 2;
	else                     regx[RIGHT] = winw - margin;

	x = regx[pos];
	for (i = 0; i < nmodules; i++) {
		if (modules[i].pos != pos) continue;
		if (pos == RIGHT) {
			x -= modules[i].w;
			modules[i].x = x;
			x -= lrpad;
		} else {
			modules[i].x = x;
			x += modules[i].w + lrpad;
		}
	}
	return 1;
}

void
draw(void)
{
	int i, j, x0, x1;
	Drw *drw = drw_create(srf);

	drw_set_scheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, winw, winh, 0, 1, 0);
	drw_set_font(drw, font);

	computelayout(LEFT);
	computelayout(CENTER);
	computelayout(RIGHT);

	for (i = 0; i < nmodules; i++)
		draw_module(drw, &modules[i], modules[i].pos == RIGHT ? 0 : winw, modules[i].pos == RIGHT);
	drw_destroy(drw);

	for (i = 0; i < 3; i++) {
		x0 = INT_MAX;
		x1 = INT_MIN;
		for (j = 0; j < nmodules; j++) {
			if (modules[j].pos != i)
				continue;
			if (modules[j].x < x0)
				x0 = modules[j].x;
			if (modules[j].x + modules[j].w > x1)
				x1 = modules[j].x + modules[j].w;
		}
		if (x0 > x1)
			x0 = x1 = regx[i]; /* region has no modules */
		regionbox[i][0] = x0;
		regionbox[i][1] = x1;
	}
}

int
draw_module(Drw *drw, Module *mod, int limit, int reverse)
{
	int w;
	int ty = (int)round((winh - (font->ascent + font->descent)) / 2.0);

	if (strcmp(mod->name, "workspaces") == 0)
		return draw_workspaces(drw, mod, limit, reverse, 0);

	w = mod->w;
	if (reverse && mod->x < limit)
		return 0;
	if (!reverse && mod->x + w > limit)
		return 0;

	drw_set_scheme(drw, scheme[SchemeSel]);
	drw_rect(drw, mod->x, marginvert, mod->w, winh - marginvert * 2, moduleradius, 1, 0);
	drw_set_scheme(drw, scheme[SchemeNorm]);
	drw_text(drw, mod->text, mod->x + padding / 2, ty, 0);

	return w;
}

int
draw_workspaces(Drw *drw, Module *mod, int limit, int reverse, int measure)
{
	int x = 0, ox;
	int i;

	if (measure) {
		for (i = 0, x = 0; i < ndesktop; i++)
			x += (i == currentdesktop ? workspaceselw : workspacepx) + workspacelrpad;
		return x - workspacelrpad;
	}

	if (!mod)
		return 0;

	x = ox = mod->x + workspacepadding / 2;
	drw_set_scheme(drw, scheme[SchemeWorkspace]);

	drw_rect(drw, mod->x, marginvert, mod->w, winh - marginvert * 2, moduleradius, 1, 0);

	for (i = 0; i < ndesktop; i++) {
		drw_set_scheme(drw, scheme[SchemeWsNorm]);
		if (workspaces[i].occ || i == currentdesktop)
			drw_set_scheme(drw, scheme[SchemeWsSel]);

		if (i == currentdesktop) {
			drw_rect(drw, x, (winh - workspaceselh) / 2,
				    workspaceselw, workspaceselh, 10, 1, 0);
			x += workspaceselw + workspacelrpad;
		} else {
			drw_rect(drw, x, (winh - workspacepx) / 2,
				    workspacepx, workspacepx, 10, 1, 0);
			x += workspacepx + workspacelrpad;
		}
		if ((!reverse && x > limit) || (reverse && x < limit))
			break;
	}
	return mod->w;
}

int gcd(int a, int b)
{
	int temp;
	while (b > 0) {
		temp = a % b;
		a = b;
		b = temp;
	}
	return a;
}

void
getcmd(Module *mod)
{
	char *cmd = mod->cmd;
	char tmpstr[CMDLENGTH] = "";
	char *s;
	int e, len;
	FILE *cmdf = popen(cmd, "r");

	if (!cmdf)
		return;

	do {
		errno = 0;
		s = fgets(tmpstr, CMDLENGTH - 1, cmdf);
		e = errno;
	} while (!s && e == EINTR);

	pclose(cmdf);

	if (!s) {
		if (mod->text[0] != '\0') {
			mod->text[0] = '\0';
			mod->dirty = 1;
		}
		return;
	}

	remove_all(tmpstr, '\n');
	len = strlen(tmpstr);
	while (len > 0 && isspace((unsigned char)tmpstr[len - 1]))
		tmpstr[--len] = '\0';

	if (strcmp(mod->text, tmpstr) != 0) {
		strcpy(mod->text, tmpstr);
		mod->dirty = 1;
	}
}

void
getcmds(int time)
{

	for (int i = 0; i < nmodules; i++) {
		if ((modules[i].interval != 0 && time % modules[i].interval == 0) || time == -1)
			getcmd(&modules[i]);
	}
}

long
getdesktop(Window w)
{
	long d = w == root ? 0 : -1; /* -1 = unknow/sticky */
	unsigned long n = 0;
	unsigned long *desktop;

	if((desktop = getprop(w, netatom[w == root ? NetCurrentDesktop : NetWMDesktop], XA_CARDINAL, &n)) && n > 0)
		d = (long)desktop[0];
	XFree(desktop);
	return d;
}

unsigned long *
getprop(Window w, Atom prop, Atom type, unsigned long *nitems)
{
	int format;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da = None;

	if (XGetWindowProperty(dpy, w, prop, 0L, 0x7fffffff, False, type,
		&da, &format, nitems, &dl, &p) == Success && p) {
		if (!p || nitems == 0 || format != 32) {
			XFree(p);
			return NULL;
		}
	}
	return (unsigned long *)p;
}

void
propertynotify(XEvent *e)
{
	XPropertyEvent *ev = &e->xproperty;
	int i;
	int newdesktop;

	if (ev->atom == netatom[NetWMDesktop]) {
		updateworkspaces();
		redraw_workspaces(0);
	}

	if(ev->window != root)
		return;
	if (ev->atom == netatom[NetCurrentDesktop]) {
		newdesktop = getdesktop(root);
		if (desktoppendingconfirm && newdesktop == currentdesktop) {
			desktoppendingconfirm = 0;  /* WM confirmed our guess, nothing changed visually */
			return;
		}
		currentdesktop = newdesktop;
		desktoppendingconfirm = 0;
		redraw_workspaces(0);
	} else if (ev->atom == netatom[NetNumberOfDesktops]) {
		updateworkspaces();  /* desktop count changed: full rebuild */
		redraw_workspaces(1);
	} else if (ev->atom == netatom[NetClientList]) {
		updateworkspaces();  /* client added/removed: recompute occ */
		redraw_workspaces(0);
	}
}

void
getsigcmds(int signal)
{
	for (int i = 0; i < nmodules; i++)
		if (modules[i].sig == signal)
			getcmd(&modules[i]);
}

void
redraw_module(Module *mod)
{
	Drw *drw = drw_create(srf);
	drw_set_font(drw, font);
	drw_clip(drw, mod->x, 0, mod->w, winh);
	drw_set_scheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, mod->x, 0, mod->w, winh, 0, 1, 0);
	draw_module(drw, mod, mod->pos == RIGHT ? 0 : winw, mod->pos == RIGHT);
	drw_unclip(drw);
	drw_destroy(drw);
}

void
redraw_region(int pos)
{
	int i, x0 = INT_MAX, x1 = INT_MIN;

	for (i = 0; i < nmodules; i++) {
		if (modules[i].pos != pos)
			continue;
		if (modules[i].x < x0)
			x0 = modules[i].x;
		if (modules[i].x + modules[i].w > x1)
			x1 = modules[i].x + modules[i].w;
	}
	if (regionbox[pos][0] < x0)
		x0 = regionbox[pos][0];
	if (regionbox[pos][1] > x1)
		x1 = regionbox[pos][1];

	Drw *drw = drw_create(srf);
	drw_set_font(drw, font);
	drw_clip(drw, x0, 0, x1 - x0, winh);
	drw_set_scheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, x0, 0, x1 - x0, winh, 0, 1, 0);
	for (i = 0; i < nmodules; i++)
		if (modules[i].pos == pos)
			draw_module(drw, &modules[i], pos == RIGHT ? 0 : winw, pos == RIGHT);
	drw_unclip(drw);
	drw_destroy(drw);

	regionbox[pos][0] = x0;
	regionbox[pos][1] = x1;
}

void
redraw_workspaces(int checkreg)
{
	int i;

	for (i = 0; i < nmodules; i++) {
		if (strcmp(modules[i].name, "workspaces") != 0)
			continue;
		if (checkreg && computelayout(modules[i].pos))
			redraw_region(modules[i].pos);
		else
			redraw_module(&modules[i]);
	}
}

void
reloadconfig(void)
{
	int i, j;

	cfg_reload(); /* re-reads file: rebuilds modules[], resets color/geometry globals to config values */

	/* colors[][] are char* into config strings; scheme[][] holds baked
	 * doubles, so it must be rebuilt whenever the hex strings could differ */
	for (i = 0; i < LENGTH(colors); i++)
		for (j = 0; j < LENGTH(colors[i]); j++)
			drw_color_create(colors[i][j], scheme[i][j]);

	/* fontname/fontsize may have changed */
	drw_font_destroy(font);
	font = drw_font_create(fontname, fontsize);

	/* height may have changed -> bar geometry, surface, strut */
	winw = sw;
	winh = height;
	XResizeWindow(dpy, barwin, (unsigned)winw, (unsigned)winh);
	drw_resize(srf, winw, winh);
	updatestrut();

	/* module list is new -> timer cadence needs recomputing */
	timerInterval = -1;
	for (i = 0; i < nmodules; i++)
		if (modules[i].interval)
			timerInterval = gcd(modules[i].interval, timerInterval);

	/* fresh modules[] have empty text until their command runs once */
	getcmds(-1);
	if (timerInterval > 0)
		alarm(timerInterval);

	memset(regionbox, 0, sizeof(regionbox));
	draw();
}

void
remove_all(char *str, char to_remove) {
	char *read = str;
	char *write = str;

	while (*read) {
		if (*read != to_remove)
			*write++ = *read;
		++read;
	}
	*write = '\0';
}

void
run(void)
{
	XEvent ev;
	int ret;

	for(int i = 0; i < nmodules; i++)
		if(modules[i].interval)
			timerInterval = gcd(modules[i].interval, timerInterval);

	getcmds(-1);
	raise(SIGALRM);

	struct pollfd pfd[] = {
		{.fd = signalFD, .events = POLLIN},
		{.fd = ConnectionNumber(dpy), .events = POLLIN},
		{.fd = cfg_watch_getfd(), .events = POLLIN}
	};

	draw();
	while (running) {
		ret = poll(pfd, sizeof(pfd) / sizeof(pfd[0]), -1);
		//if (ret < 0 || !(pfd[0].revents & POLLIN))
		//	break;
		if (pfd[0].revents & POLLIN)
			sighandler();
		if (pfd[2].revents & POLLIN && cfg_watch_handle())
			reloadconfig();
		while (running && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			case ButtonPress:
				buttonpress(&ev);
				break;
			case Expose:
				if (ev.xexpose.count == 0)
					draw();
				break;
			case PropertyNotify:
				propertynotify(&ev);
				break;
			}
		}
	}
}

void
setup(void)
{
	XVisualInfo vinfo;
	sigset_t signals;
	int i, j;

	sigemptyset(&signals);
	sigaddset(&signals, SIGALRM); /* timer events */

	/* module signals */
	for (i = 0; i < nmodules; i++)
		if (modules[i].sig > 0)
			sigaddset(&signals, SIGRTMIN + modules[i].sig);

	signalFD = signalfd(-1, &signals, 0);

	for (i = SIGRTMIN; i <= SIGRTMAX; i++)
		sigaddset(&signals, i);

	sigprocmask(SIG_BLOCK, &signals, NULL);

	struct sigaction sigchld_action = {
  		.sa_handler = SIG_DFL,
  		.sa_flags = SA_NOCLDWAIT
	};
	sigaction(SIGCHLD, &sigchld_action, NULL);

	signal(SIGHUP, sighup);
	signal(SIGTERM, sigterm);

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
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);

	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetClientListStacking] = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
	netatom[NetWMDesktop] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);

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

	font = drw_font_create(fontname, fontsize);
	XSelectInput(dpy, root, PropertyChangeMask);

	currentdesktop = getdesktop(root);
	updateworkspaces();
	setupwindow();
	cfg_watch_init();
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
	winh = height;

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
sighandler(void)
{
	static int time = 0;
	struct signalfd_siginfo si;
	int ret = read(signalFD, &si, sizeof(si));
	int signal = si.ssi_signo;
	int i, j;
	int regiondirty[3] = {0};

	if (ret < 0) return;

	if (signal == SIGALRM) {
		getcmds(time);
		alarm(timerInterval);
		time += timerInterval;
	} else {
		getsigcmds(signal - SIGRTMIN);
	}

	for (i = 0; i < nmodules; i++)
		if (modules[i].dirty)
			regiondirty[modules[i].pos] = 1;

	for (i = 0; i < 3; i++) {
		if (!regiondirty[i])
			continue;
		if (computelayout(i)) {
			redraw_region(i); /* width changed */
		} else {
			for (j = 0; j < nmodules; j++)
				if (modules[j].pos == i && modules[j].dirty)
					redraw_module(&modules[j]);
		}
	}

	for (i = 0; i < nmodules; i++)
		modules[i].dirty = 0;
}

void
sighup(int unnused)
{
	running = 0;
	restart = 1;
}

void
sigterm(int unnused)
{
	running = 0;
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

void
updateworkspaces(void)
{
	unsigned long i, n = 0;
	unsigned long *prop = NULL;
	long d;

	updatedesktops();
	free(workspaces);
	workspaces = ecalloc(1, sizeof(Workspace) * ndesktop);

	for(i = 0; i < ndesktop; i++)
		workspaces[i].n = i;
	if (!(prop = getprop(root, netatom[NetClientListStacking], XA_WINDOW, &n)))
		if (!(prop = getprop(root, netatom[NetClientList], XA_WINDOW, &n)))
			return;
	for (i = 0; i < n; i++) {
		XSelectInput(dpy, (Window)prop[i], PropertyChangeMask);
		if ((d = getdesktop((Window)prop[i])) >= 0)
			workspaces[d].occ = 1;
	}

	XFree(prop);
}

void
view(long workspace)
{
	XEvent e = { 0 };
	int i;

	e.xclient.type = ClientMessage;
	e.xclient.window = root;
	e.xclient.message_type = netatom[NetCurrentDesktop];
	e.xclient.format = 32;
	e.xclient.data.l[0] = workspace;
	e.xclient.data.l[1] = CurrentTime;

	currentdesktop = workspace;
	redraw_workspaces(0);
	desktoppendingconfirm = 1;
	XSendEvent(dpy, root, False,
			SubstructureRedirectMask|SubstructureNotifyMask, &e);
}

int
main(int argc, char **argv)
{
	if (!(dpy = XOpenDisplay(NULL)))
    		die("ambar: cannot open display");
	cfg_load();
	cfg_load_xresources();
	setup();
	run();
	cfg_watch_cleanup();
	close(signalFD);
	if(restart) execvp(argv[0], argv);
	XCloseDisplay(dpy);
	return 0;
}

