#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>
//
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <cairo/cairo-xlib.h>

#include "util.h"
#include "drw.h"

void
drw_button(Drw *drw, char *text, int x, int y, int w, int h, int r, int linew)
{
	double ty = (int)round(y + (h - (drw->fnt->ascent + drw->fnt->descent)) / 2.0 + drw->fnt->ascent);
	double tx = (int)round(x + (w - drw_text_getwidth(drw->fnt, text)) / 2);

	drw_rect(drw, x, y, w, h, r, 1, 0);
	if (linew)
		drw_rect(drw, x, y, w, h, r, 0, linew);
	drw_text(drw, text, tx, ty);

	return;
}

void
drw_color_create(char *hex, double *rgba)
{
	unsigned int a, ri, gi, bi;

	if (sscanf(hex + 1, "%02x%02x%02x%02x", &a, &ri, &gi, &bi) != 4) {
		if (sscanf(hex + 1, "%02x%02x%02x", &ri, &gi, &bi) != 3) {
			ri = 255;
			gi = 0;
			bi = 255;
		}
		a = 255;
	}

	rgba[0] = ri / 255.0;
	rgba[1] = gi / 255.0;
	rgba[2] = bi / 255.0;
	rgba[3] = a / 255.0;
}

Drw *
drw_create(Surf *s)
{
	Drw *drw;

	drw = ecalloc(1, sizeof(Drw));
	drw->surf = s;
	drw->cr = cairo_create(s);
	cairo_set_operator(drw->cr, CAIRO_OPERATOR_SOURCE);

	return drw;
}

void
drw_destroy(Drw *drw)
{
	cairo_destroy(drw->cr);
	cairo_surface_flush(drw->surf);
	free(drw);
}

Fnt *
drw_font_create(char *fontname, double fontsize)
{
	PangoFontMetrics *metrics;
	PangoContext *pctx;
	cairo_surface_t *tmp;
	cairo_t *cr;
	Fnt *f;

	tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cr = cairo_create(tmp);

	f = ecalloc(1, sizeof(Fnt));
	f->name = fontname;
	f->size = fontsize;

	f->desc = pango_font_description_from_string(fontname);
	pango_font_description_set_absolute_size(f->desc, fontsize * PANGO_SCALE);

	pctx = pango_cairo_create_context(cr);
	metrics = pango_context_get_metrics(pctx, f->desc, NULL);

	f->ascent = (double)pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
	f->descent = (double)pango_font_metrics_get_descent(metrics) / PANGO_SCALE;
	f->h = f->ascent + f->descent;

	pango_font_metrics_unref(metrics);
	g_object_unref(pctx);
	cairo_destroy(cr);
	cairo_surface_destroy(tmp);

	return f;
}

void
drw_font_destroy(Fnt *fnt)
{
	if (!fnt)
		return;
	if (fnt->desc)
		pango_font_description_free(fnt->desc);
	free(fnt);
}

Icon *
drw_icon_create(unsigned long *prop, unsigned long len, unsigned int reqsize)
{
	unsigned long i, x, y, bestoff = 0, bestw = 0, besth = 0;
	long diff, bestdiff = -1;
	unsigned char *buf;
	uint32_t *row;
	int stride;
	Icon *icon;

	i = 0;
	while (i + 2 <= len) {
		unsigned long w = prop[i], h = prop[i + 1];
		if (w == 0 || h == 0 || w > 4096 || h > 4096 || i + 2 + w * h > len)
			break;
		diff = (long)w - (long)reqsize;
		if (diff < 0)
			diff = -diff;
		if (bestdiff < 0 || diff < bestdiff || (diff == bestdiff && w > bestw)) {
			bestdiff = diff;
			bestoff = i + 2;
			bestw = w;
			besth = h;
		}
		i += 2 + w * h;
	}

	if (bestw == 0 || besth == 0)
		return NULL;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, bestw);
	buf = ecalloc(1, (size_t)stride * besth);

	for (y = 0; y < besth; y++) {
		row = (uint32_t *)(buf + y * stride);
		for (x = 0; x < bestw; x++) {
			unsigned long px = prop[bestoff + y * bestw + x];
			unsigned int a = (px >> 24) & 0xff;
			unsigned int r = (px >> 16) & 0xff;
			unsigned int g = (px >>  8) & 0xff;
			unsigned int b =  px        & 0xff;

			/* premultiply, since cairo's ARGB32 expects it */
			r = (r * a) / 255;
			g = (g * a) / 255;
			b = (b * a) / 255;
			row[x] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	icon = ecalloc(1, sizeof(Icon));
	icon->w = (unsigned int)bestw;
	icon->h = (unsigned int)besth;
	icon->data = buf;
	icon->surf = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32,
			(int)bestw, (int)besth, stride);
	return icon;
}

void
drw_icon(Drw *drw, Icon *icon, int x, int y, int w, int h)
{
	cairo_pattern_t *pat;

	if (!icon || !icon->surf || w <= 0 || h <= 0)
		return;

	cairo_save(drw->cr);
	cairo_translate(drw->cr, x, y);
	cairo_scale(drw->cr, (double)w / icon->w, (double)h / icon->h);
	cairo_set_source_surface(drw->cr, icon->surf, 0, 0);
	pat = cairo_get_source(drw->cr);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_GOOD);
	cairo_paint(drw->cr);
	cairo_restore(drw->cr);
}

void
drw_icon_destroy(Icon *icon)
{
	if (!icon)
		return;
	if (icon->surf)
		cairo_surface_destroy(icon->surf);
	free(icon->data);
	free(icon);
}

unsigned long
drw_rgba_to_argb(double color[4])
{
	return ((unsigned long)(color[3] * 255.0 + 0.5) << 24) |
		  ((unsigned long)(color[0] * 255.0 + 0.5) << 16) |
		  ((unsigned long)(color[1] * 255.0 + 0.5) <<  8) |
		  (unsigned long)(color[2] * 255.0 + 0.5);
}

void
drw_resize(Surf *s, int w, int h)
{
	cairo_xlib_surface_set_size(s, w, h);
}

void
drw_rect(Drw *drw, int x, int y, int w, int h, int r, int fill, int linew)
{
	double degrees = M_PI / 180.0;

	drw_set_color(drw, drw->scm[fill ? ColBg : ColBorder]);

	/* clamp radius so it never exceeds half the smaller dimension */
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;

	if (r <= 0) {
		/* fall back to a plain rectangle */
		cairo_rectangle(drw->cr, x, y, w, h);
	} else {
		cairo_new_sub_path(drw->cr);
		cairo_arc(drw->cr, x + w - r, y + r,     r, -90 * degrees,   0 * degrees);
		cairo_arc(drw->cr, x + w - r, y + h - r, r,   0 * degrees,  90 * degrees);
		cairo_arc(drw->cr, x + r,     y + h - r, r,  90 * degrees, 180 * degrees);
		cairo_arc(drw->cr, x + r,     y + r,     r, 180 * degrees, 270 * degrees);
		cairo_close_path(drw->cr);
	}

	if (fill) {
		cairo_fill(drw->cr);
	} else if (linew > 0) {
		cairo_set_line_width(drw->cr, linew);
		cairo_stroke(drw->cr);
	}
}

void
drw_set_color(Drw *drw, double *col)
{
	cairo_set_source_rgba(drw->cr, col[0], col[1], col[2], col[3]);
}

void
drw_set_font(Drw *drw, Fnt *fnt)
{
	drw->fnt = fnt;
}

void
drw_set_scheme(Drw *drw, double scheme[3][4])
{
	if (drw) drw->scm = scheme;
}

Surf *
drw_surf_create(Display *dpy, Drawable d, Visual *v, int w, int h)
{
	return cairo_xlib_surface_create(dpy, d, v, w, h);
}

void
drw_surf_destroy(Surf *s)
{
	cairo_surface_destroy(s);
}

void
drw_text(Drw *drw, char *text, int x, int y)
{
	PangoLayout *layout;

	drw_set_color(drw, drw->scm[ColFg]);

	layout = pango_cairo_create_layout(drw->cr);
	pango_layout_set_font_description(layout, drw->fnt->desc);
	pango_layout_set_text(layout, text, -1);

	cairo_move_to(drw->cr, x, y);
	pango_cairo_show_layout(drw->cr, layout);

	g_object_unref(layout);
}

void
drw_text_clamp(Drw *drw, char *text, int w, size_t textsize)
{
	const char *ellipsis = "...";
	char buf[512];
	int avail, len = 0, newlen;
	int tw, th, ew, eh, cw;
	PangoLayout *layout;

	layout = pango_cairo_create_layout(drw->cr);
	pango_layout_set_font_description(layout, drw->fnt->desc);

	pango_layout_set_text(layout, text, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);

	if (tw <= w) {
		g_object_unref(layout);
		return;
	}

	pango_layout_set_text(layout, ellipsis, -1);
	pango_layout_get_pixel_size(layout, &ew, &eh);

	avail = w - ew;
	if (avail <= 0) {
		text[0] = '\0';
		g_object_unref(layout);
		return;
	}

	while (text[len]) {
		newlen = len + 1;
		while (text[newlen] && (text[newlen] & 0xC0) == 0x80)
			newlen++;

		snprintf(buf, sizeof(buf), "%.*s", newlen, text);
		pango_layout_set_text(layout, buf, -1);
		pango_layout_get_pixel_size(layout, &cw, &eh);

		if (cw > avail)
			break;

		len = newlen;
	}

	snprintf(buf, sizeof(buf), "%.*s%s", len, text, ellipsis);
	strncpy(text, buf, textsize - 1);
	text[textsize - 1] = '\0';

	g_object_unref(layout);
}

int
drw_text_getwidth(Fnt *fnt, char *text)
{
	PangoLayout *layout;
	cairo_surface_t *tmp;
	cairo_t *cr;
	int w, h;

	tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cr = cairo_create(tmp);

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, fnt->desc);
	pango_layout_set_text(layout, text, -1);
	pango_layout_get_pixel_size(layout, &w, &h);

	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(tmp);

	return w;
}

unsigned long
drw_x11_color_create(char *hex)
{
	unsigned int clr;

	if (!sscanf(hex + 1, "%08x", &clr))
		if (!sscanf(hex + 1, "%06x", &clr))
			return 0xffff00ff;
	return clr | 0xff << 24;
}

