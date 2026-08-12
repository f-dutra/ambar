#include <cairo/cairo.h>
#include <pango/pango.h>

#define	TEXTW(FNT, x)	(drw_text_getwidth((FNT), (x)))

enum { ColFg, ColBg, ColBorder };

typedef struct {
	char *name;
	double size, h, ascent, descent;
	PangoFontDescription *desc;
} Fnt;

typedef cairo_surface_t Surf;

typedef struct {
	cairo_t *cr;
	Surf *surf;
	Fnt *fnt;
	double (*scm)[4];
} Drw;

typedef struct {
	unsigned int w, h;
	unsigned char *data; /* ARGB32 */
	cairo_surface_t *surf;
} Icon;

void drw_button(Drw *drw, char *text, int x, int y, int w, int h, int r, int linew);
void drw_color_create(char *hex, double *rgba);
Drw *drw_create(Surf *s);
void drw_destroy(Drw *drw);
Fnt *drw_font_create(char *fontname, double fontsize);
void drw_font_destroy(Fnt *fnt);
Icon *drw_icon_create(unsigned long *prop, unsigned long len, unsigned int reqsize);
void drw_icon(Drw *drw, Icon *icon, int x, int y, int w, int h);
void drw_icon_destroy(Icon *icon);
unsigned long drw_rgba_to_argb(double color[4]);
void drw_rect(Drw *drw, int x, int y, int w, int h, int r, int fill, int linew);
void drw_resize(Surf *s, int w, int h);
void drw_scheme_setup();
void drw_set_font(Drw *drw, Fnt *fnt);
void drw_set_color(Drw *drw, double *col);
void drw_set_scheme(Drw *drw, double scheme[3][4]);
Surf *drw_surf_create(Display *dpy, Drawable d, Visual *v, int w, int h);
void drw_surf_destroy(Surf *s);
void drw_text(Drw *drw, char *text, int x, int y);
void drw_text_clamp(Drw *drw, char *text, int w, size_t textsize);
int drw_text_getwidth(Fnt *fnt, char *text);
unsigned long drw_x11_color_create(char *clr);

