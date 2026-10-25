/* Original X11 presentation/font backend. Windows ABI lives in the bridge. */
#include "protocol.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <sys/socket.h>
#include <sys/prctl.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
struct native_menu_entry;
struct surface {
	struct native_menu_entry *menu_items;
	int menu_height;
	bool painting_menu;
	uint32_t root_menu, active_menu;
	Window popup;
	int popup_x, popup_y, popup_width, popup_height;
	struct surface *next;
	uint64_t id;
	Window window;
	GC gc;
	Pixmap backbuffer;
	int back_width, back_height;
	XImage *image;
	int width, height;
	FT_Face face;
	int font_height;
};
static Display *display;
static struct surface *surfaces;
static Atom wm_delete;
static FT_Library ft;
static int xerror;
static int on_error(Display *d, XErrorEvent *e)
{
	(void)d;
	xerror = e->error_code;
	return 0;
}
static struct surface *lookup(uint64_t id)
{
	for (struct surface *s = surfaces; s; s = s->next)
		if (s->id == id)
			return s;
	return NULL;
}
static struct surface *by_window(Window window)
{
	for (struct surface *s = surfaces; s; s = s->next)
		if (s->window == window || s->popup == window)
			return s;
	return NULL;
}
static unsigned long component(unsigned value, unsigned long mask)
{
	if (!mask)
		return 0;
	unsigned shift = 0;
	while (!(mask & 1)) {
		mask >>= 1;
		shift++;
	}
	return ((value * mask + 127) / 255) << shift;
}
static unsigned long native_pixel(XImage *im, uint32_t rgb)
{
	return component((rgb >> 16) & 255, im->red_mask) |
	       component((rgb >> 8) & 255, im->green_mask) |
	       component(rgb & 255, im->blue_mask);
}
static unsigned channel(unsigned long pixel, unsigned long mask)
{
	if (!mask)
		return 0;
	while (!(mask & 1)) {
		mask >>= 1;
		pixel >>= 1;
	}
	return ((pixel & mask) * 255 + mask / 2) / mask;
}
static uint32_t rgb_pixel(XImage *im, int x, int y)
{
	unsigned long p = XGetPixel(im, x, y);
	return channel(p, im->red_mask) << 16 |
	       channel(p, im->green_mask) << 8 | channel(p, im->blue_mask);
}
static void pixel(struct surface *s, int x, int y, uint32_t rgb)
{
	if (x >= 0 && y >= (s->painting_menu ? 0 : s->menu_height) &&
	    x < s->width && y < s->height)
		XPutPixel(s->image, x, y, native_pixel(s->image, rgb));
}
static int resize_surface(struct surface *s, int width, int height)
{
	if (width < 1 || height < 1 || width > PG_MAX_DIM ||
	    height > PG_MAX_DIM)
		return 87;
	XImage *im = XCreateImage(
		display, DefaultVisual(display, DefaultScreen(display)),
		DefaultDepth(display, DefaultScreen(display)), ZPixmap, 0, NULL,
		width, height, 32, 0);
	if (!im)
		return 8;
	im->data = calloc(height, im->bytes_per_line);
	if (!im->data) {
		im->data = NULL;
		XDestroyImage(im);
		return 8;
	}
	if (s->image) {
		int w = width < s->width ? width : s->width,
		    h = height < s->height ? height : s->height;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				XPutPixel(im, x, y, XGetPixel(s->image, x, y));
		XDestroyImage(s->image);
	}
	s->image = im;
	s->width = width;
	s->height = height;
	return 0;
}
static void fill(struct surface *s, int x, int y, int right, int bottom,
		 uint32_t rgb)
{
	if (x < 0)
		x = 0;
	if (y < (s->painting_menu ? 0 : s->menu_height))
		y = s->painting_menu ? 0 : s->menu_height;
	if (right > s->width)
		right = s->width;
	if (bottom > s->height)
		bottom = s->height;
	unsigned long p = native_pixel(s->image, rgb);
	for (int row = y; row < bottom; row++)
		for (int col = x; col < right; col++)
			XPutPixel(s->image, col, row, p);
}
static int font(struct surface *s, const char *name, int height)
{
	if (height < 1)
		height = 12;
	if (height > 256)
		height = 256;
	FcPattern *pattern =
		FcNameParse((const FcChar8 *)(*name ? name : "sans"));
	if (!pattern)
		return 8;
	FcConfigSubstitute(NULL, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);
	FcResult result;
	FcPattern *match = FcFontMatch(NULL, pattern, &result);
	FcPatternDestroy(pattern);
	if (!match)
		return 2;
	FcChar8 *file;
	int index = 0;
	FcPatternGetInteger(match, FC_INDEX, 0, &index);
	FT_Face face = NULL;
	int r = FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch ?
			FT_New_Face(ft, (const char *)file, index, &face) :
			1;
	FcPatternDestroy(match);
	if (r)
		return 2;
	if (FT_Set_Pixel_Sizes(face, 0, height)) {
		FT_Done_Face(face);
		return 87;
	}
	if (s->face)
		FT_Done_Face(s->face);
	s->face = face;
	s->font_height = height;
	return 0;
}
static uint32_t next_utf8(const unsigned char **p, const unsigned char *end)
{
	uint32_t c = *(*p)++;
	if (c < 128)
		return c;
	unsigned n = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
	c &= n == 1 ? 31 : n == 2 ? 15 : 7;
	while (n-- && *p < end)
		c = (c << 6) | (*(*p)++ & 63);
	return c;
}
static void text_out(struct surface *s, struct pg_packet *p)
{
	int64_t requested = p->c;
	int height = requested < 0 ? (requested < -256 ? 256 : -requested) :
				     (requested > 256 ? 256 : requested);
	if (height < 1)
		height = 12;
	char *family = p->text + p->length + 1;
	p->status = font(s, family, height);
	if (p->status)
		return;
	int asc = s->face->size->metrics.ascender >> 6, pen = p->a;
	const unsigned char *cursor = (void *)p->text,
			    *end = cursor + p->length;
	while (cursor < end) {
		uint32_t cp = next_utf8(&cursor, end);
		if (FT_Load_Char(s->face, cp, FT_LOAD_RENDER))
			continue;
		FT_GlyphSlot g = s->face->glyph;
		int advance = g->advance.x >> 6;
		if (p->flags == 2)
			fill(s, pen, p->b, pen + advance, p->b + height,
			     p->background);
		for (unsigned row = 0; row < g->bitmap.rows; row++)
			for (unsigned col = 0; col < g->bitmap.width; col++) {
				int x = pen + g->bitmap_left + (int)col,
				    y = p->b + asc - g->bitmap_top + (int)row;
				if (x < 0 || y < 0 || x >= s->width ||
				    y >= s->height)
					continue;
				unsigned alpha =
					g->bitmap.pixel_mode ==
							FT_PIXEL_MODE_MONO ?
						((g->bitmap.buffer
							  [row * g->bitmap.pitch +
							   col / 8] &
						  (128 >> (col % 8))) ?
							 255 :
							 0) :
						g->bitmap.buffer
							[row * g->bitmap.pitch +
							 col];
				uint32_t old = rgb_pixel(s->image, x, y),
					 out = 0;
				for (int shift = 0; shift <= 16; shift += 8)
					out |= (((((p->color >> shift) & 255) *
							  alpha +
						  ((old >> shift) & 255) *
							  (255 - alpha)) +
						 127) /
						255)
					       << shift;
				pixel(s, x, y, out);
			}
		pen += advance;
	}
	p->a = pen - p->a;
}
static unsigned vk(KeySym sym)
{
	if (sym >= 'a' && sym <= 'z')
		return sym - 32;
	if (sym >= 0x20 && sym <= 0x7e)
		return sym;
	if (sym >= XK_F1 && sym <= XK_F24)
		return 0x70 + sym - XK_F1;
	switch (sym) {
	case XK_Return:
		return 13;
	case XK_Escape:
		return 27;
	case XK_BackSpace:
		return 8;
	case XK_Tab:
		return 9;
	case XK_Left:
		return 37;
	case XK_Up:
		return 38;
	case XK_Right:
		return 39;
	case XK_Down:
		return 40;
	case XK_Home:
		return 36;
	case XK_End:
		return 35;
	case XK_Delete:
		return 46;
	case XK_Insert:
		return 45;
	case XK_Shift_L:
	case XK_Shift_R:
		return 16;
	case XK_Control_L:
	case XK_Control_R:
		return 17;
	case XK_Alt_L:
	case XK_Alt_R:
		return 18;
	default:
		return 0;
	}
}
#include "host_menus.inc"
static void present_surface(struct surface *s)
{
	if (!s->backbuffer || s->back_width != s->width ||
	    s->back_height != s->height) {
		if (s->backbuffer)
			XFreePixmap(display, s->backbuffer);
		s->backbuffer = XCreatePixmap(
			display, s->window, s->width, s->height,
			DefaultDepth(display, DefaultScreen(display)));
		s->back_width = s->width;
		s->back_height = s->height;
	}
	XPutImage(display, s->backbuffer, s->gc, s->image, 0, 0, 0, 0, s->width,
		  s->height);
	XCopyArea(display, s->backbuffer, s->window, s->gc, 0, 0, s->width,
		  s->height, 0, 0);
}
static void event_packet(struct pg_packet *p)
{
	if (!display) {
		if (p->a > 0) poll(NULL,0,p->a > 16 ? 16 : p->a);
		p->op=PG_EVENT; p->id=0; p->a=0; return;
	}
	struct pollfd fd = { ConnectionNumber(display), POLLIN, 0 };
	if (!XPending(display) && p->a > 0)
		poll(&fd, 1, p->a > 16 ? 16 : p->a);
	p->op = PG_EVENT;
	p->id = 0;
	p->a = 0;
	while (XPending(display)) {
		XEvent e;
		XNextEvent(display, &e);
		struct surface *s = by_window(e.xany.window);
		if (!s)
			continue;
		p->id = s->id;
		p->b = p->c = p->d = 0;
		p->flags = 0;
		int menu_event = native_menu_event(s, &e, p);
		if (menu_event == 2)
			return;
		if (menu_event == 1)
			continue;
		switch (e.type) {
		case Expose:
			if (e.xexpose.count)
				break;
			p->a = 0xf;
			return;
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] != wm_delete)
				break;
			p->a = 0x10;
			return;
		case ConfigureNotify: {
			XEvent latest;
			while (XCheckTypedWindowEvent(display, s->window,
						      ConfigureNotify, &latest))
				e = latest;
		}
			if (e.xconfigure.width == s->width &&
			    e.xconfigure.height == s->height)
				break;
			p->status = resize_surface(s, e.xconfigure.width,
						   e.xconfigure.height);
			if (p->status)
				return;
			p->a = 5;
			p->c = s->width;
			p->d = s->height;
			return;
		case MotionNotify:
			p->a = 0x200;
			p->c = e.xmotion.x;
			p->d = e.xmotion.y;
			return;
		case ButtonPress:
		case ButtonRelease:
			if (e.xbutton.button > 3)
				break;
			p->a = (e.xbutton.button == 1 ? 0x201 :
				e.xbutton.button == 2 ? 0x207 :
							0x204) +
			       (e.type == ButtonRelease);
			p->c = e.xbutton.x;
			p->d = e.xbutton.y;
			return;
		case FocusIn:
			p->a = 7;
			return;
		case FocusOut:
			p->a = 8;
			return;
		case KeyPress:
		case KeyRelease: {
			char text[16];
			KeySym sym;
			int n = XLookupString(&e.xkey, text, sizeof(text), &sym,
					      NULL);
			p->a = e.type == KeyPress ? 0x100 : 0x101;
			p->b = vk(sym);
			p->c = 1 | (e.xkey.keycode << 16);
			if (e.type == KeyRelease)
				p->c |= 0xc0000000;
			if (n == 1 && (unsigned char)text[0] < 128 &&
			    e.type == KeyPress)
				p->flags = (unsigned char)text[0];
			return;
		}
		default:
			break;
		}
	}
	p->id = 0;
	p->a = 0;
}
#include "host_metrics.inc"
int main(void)
{
	if (getppid() == 1)
		return 1;
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	signal(SIGPIPE, SIG_IGN);
	struct pg_packet p = { .magic = PG_MAGIC, .op = PG_HELLO,
		.status = FT_Init_FreeType(&ft) ? 8 : 0 };
	if (send(3, &p, sizeof(p), MSG_NOSIGNAL) != (ssize_t)sizeof(p) ||
	    p.status)
		return 1;
	while (1) {
		ssize_t n = recv(3, &p, sizeof(p), MSG_TRUNC);
		if (n == 0)
			break;
		if (n < 0 && errno == EINTR)
			continue;
		if (n != (ssize_t)sizeof(p) || p.magic != PG_MAGIC ||
		    p.length >= PG_TEXT)
			break;
		p.text[PG_TEXT - 1] = 0;
		p.status = 0;
		xerror = 0;
		struct surface *s = lookup(p.id);
		if (p.op != PG_CREATE && p.op != PG_POLL && p.op != PG_FONT_METRICS && p.op != PG_MEASURE && !s)
			p.status = 6;
		if (!p.status)
			switch (p.op) {
			case PG_FONT_METRICS:
			case PG_MEASURE:
				native_font_query(&p);
				break;
			case PG_CREATE:
				if (!display) {
					display = XOpenDisplay(NULL);
					if (!display) { p.status=126; break; }
					XSetErrorHandler(on_error);
					wm_delete=XInternAtom(display,"WM_DELETE_WINDOW",False);
				}
				if (s || !p.id || p.c < 1 || p.d < 1 ||
				    p.c > PG_MAX_DIM || p.d > PG_MAX_DIM) {
					p.status = 87;
					break;
				}
				s = calloc(1, sizeof(*s));
				if (!s) {
					p.status = 8;
					break;
				}
				s->id = p.id;
				p.status = resize_surface(s, p.c, p.d);
				if (p.status) {
					free(s);
					break;
				}
				s->window = XCreateSimpleWindow(
					display, DefaultRootWindow(display),
					p.a, p.b, p.c, p.d, 0, 0, 0);
				s->gc = XCreateGC(display, s->window, 0, NULL);
				XSetWindowBackgroundPixmap(display, s->window,
							   None);
				XSetWindowAttributes retained = {
					.bit_gravity = NorthWestGravity
				};
				XChangeWindowAttributes(display, s->window,
							CWBitGravity,
							&retained);
				XSetGraphicsExposures(display, s->gc, False);
				XStoreName(display, s->window, p.text);
				XSetWMProtocols(display, s->window, &wm_delete,
						1);
				XClassHint hint = { "binfmt_pe", "BinfmtPE" };
				XSetClassHint(display, s->window, &hint);
				XSelectInput(
					display, s->window,
					ExposureMask | StructureNotifyMask |
						KeyPressMask | KeyReleaseMask |
						ButtonPressMask |
						ButtonReleaseMask |
						PointerMotionMask |
						FocusChangeMask);
				s->next = surfaces;
				surfaces = s;
				break;
			case PG_DESTROY: {
				struct surface **link = &surfaces;
				while (*link != s)
					link = &(*link)->next;
				*link = s->next;
				native_menu_clear(s);
				XDestroyWindow(display, s->window);
				if (s->backbuffer)
					XFreePixmap(display, s->backbuffer);
				XFreeGC(display, s->gc);
				XDestroyImage(s->image);
				if (s->face)
					FT_Done_Face(s->face);
				free(s);
				break;
			}
			case PG_SHOW:
				if (p.a)
					XMapRaised(display, s->window);
				else
					XUnmapWindow(display, s->window);
				break;
			case PG_TITLE:
				XStoreName(display, s->window, p.text);
				break;
			case PG_FILL:
				fill(s, p.a, p.b, p.c, p.d, p.color);
				break;
			case PG_PIXEL:
				if (p.a < 0 || p.b < 0 || p.a >= s->width ||
				    p.b >= s->height)
					p.status = 87;
				else
					pixel(s, p.a, p.b, p.color);
				break;
			case PG_GET_PIXEL:
				if (p.a < 0 || p.b < 0 || p.a >= s->width ||
				    p.b >= s->height)
					p.status = 87;
				else
					p.color = rgb_pixel(s->image, p.a, p.b);
				break;
			case PG_LINE: {
				if (p.a < -32768 || p.a > 32767 ||
				    p.b < -32768 || p.b > 32767 ||
				    p.c < -32768 || p.c > 32767 ||
				    p.d < -32768 || p.d > 32767) {
					p.status = 87;
					break;
				}

				int x = p.a, y = p.b, dx = abs(p.c - x),
				    sx = x < p.c ? 1 : -1, dy = -abs(p.d - y),
				    sy = y < p.d ? 1 : -1, err = dx + dy;
				while (x != p.c || y != p.d) {
					pixel(s, x, y, p.color);
					int e = 2 * err;
					if (e >= dy) {
						err += dy;
						x += sx;
					}
					if (e <= dx) {
						err += dx;
						y += sy;
					}
				}
				break;
			}
			case PG_TEXT_OUT:
				if (p.length + 1 >= PG_TEXT ||
				    !memchr(p.text + p.length + 1, 0,
					    PG_TEXT - p.length - 1)) {
					p.status = 87;
					break;
				}
				text_out(s, &p);
				break;
			case PG_PRESENT:
				native_menu_bar(s);
				present_surface(s);
				break;
			case PG_MENU_BEGIN:
				native_menu_clear(s);
				s->root_menu = p.a;
				s->menu_height = p.b == 24 ? 24 : 0;
				break;
			case PG_MENU_ITEM: {
				struct native_menu_entry *item =
					calloc(1, sizeof(*item));
				if (!item) {
					p.status = 8;
					break;
				}
				item->menu = p.d;
				item->index = p.a;
				item->command = p.b;
				item->submenu = p.c;
				item->flags = p.flags;
				memcpy(item->text, p.text, PG_TEXT);
				item->next = s->menu_items;
				s->menu_items = item;
				break;
			}
			case PG_MENU_END:
				native_menu_bar(s);
				present_surface(s);
				break;
			case PG_POSITION:
				p.status = resize_surface(s, p.c, p.d);
				if (!p.status)
					XMoveResizeWindow(display, s->window,
							  p.a, p.b, p.c, p.d);
				break;
			case PG_RESIZE:
				p.status = resize_surface(s, p.c, p.d);
				if (!p.status)
					XResizeWindow(display, s->window, p.c,
						      p.d);
				break;
			case PG_VERIFY: {
				if (p.a < 0 || p.b < 0 || p.a >= s->width ||
				    p.b >= s->height) {
					p.status = 87;
					break;
				}

				XImage *im = XGetImage(display, s->window, 0, 0,
						       s->width, s->height,
						       AllPlanes, ZPixmap);
				if (!im) {
					p.status = 31;
					break;
				}
				p.color = rgb_pixel(im, p.a, p.b);
				XDestroyImage(im);
				break;
			}
			case PG_POLL:
				event_packet(&p);
				break;
			default:
				p.status = 50;
				break;
			}
		if (display) XSync(display, False);
		if (xerror)
			p.status = 31;
		if (send(3, &p, sizeof(p), MSG_NOSIGNAL) != (ssize_t)sizeof(p))
			break;
	}
	if (display) XCloseDisplay(display);
	FT_Done_FreeType(ft);
	return 0;
}
