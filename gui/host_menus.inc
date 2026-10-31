/* Original menu-strip/popup presenter. The bridge sends data, never PE pointers. */
struct native_menu_entry {
	struct native_menu_entry *next;
	uint32_t menu, submenu, command, flags;
	unsigned index;
	char text[PG_TEXT];
};
static void present_surface(struct surface *s);
static void native_menu_close(struct surface *s)
{
	if (s->popup) {
		XUngrabPointer(display, CurrentTime);
		XDestroyWindow(display, s->popup);
		s->popup = 0;
	}
	s->active_menu = 0;
}
static void native_menu_clear(struct surface *s)
{
	native_menu_close(s);
	while (s->menu_items) {
		struct native_menu_entry *e = s->menu_items;
		s->menu_items = e->next;
		free(e);
	}
}
static struct native_menu_entry *native_menu_item(struct surface *s,
						  uint32_t menu, unsigned index)
{
	for (struct native_menu_entry *e = s->menu_items; e; e = e->next)
		if (e->menu == menu && e->index == index)
			return e;
	return NULL;
}
static void menu_label(char *out, const char *text)
{
	unsigned n = 0;
	for (unsigned i = 0; text[i] && text[i]!='\t' && n < PG_TEXT - 1; i++) {
		if (text[i] == '&') {
			if (text[i + 1] == '&')
				i++;
			else
				continue;
		}
		out[n++] = text[i];
	}
	out[n] = 0;
}
static int menu_width(const char *text)
{
	int n = 0;
	for (; *text; text++)
		if (*text=='\t') n+=3;
		else if (((unsigned char)*text & 0xc0) != 0x80)
			n++;
	return n * 8 + 20;
}
static void menu_text(struct surface *s, int x, int y, const char *text,
		      uint32_t color)
{
	struct pg_packet p = {
		.a = x, .b = y, .c = 13, .color = color, .flags = 1
	};
	menu_label(p.text, text);
	p.length = strlen(p.text);
	if (p.length > PG_TEXT - 7)
		p.length = PG_TEXT - 7;
	p.text[p.length] = 0;
	memcpy(p.text + p.length + 1, "sans", 5);
	text_out(s, &p);
}
static void native_menu_bar(struct surface *s)
{
	if (!s->menu_height)
		return;
	s->painting_menu = true;
	fill(s, 0, 0, s->width, s->menu_height, 0xf0f0f0);
	int x = 4;
	for (unsigned i = 0;; i++) {
		struct native_menu_entry *e =
			native_menu_item(s, s->root_menu, i);
		if (!e)
			break;
		int width = menu_width(e->text);
		menu_text(s, x + 8, 4, e->text,
			  (e->flags & 3) ? 0x888888 : 0x202020);
		x += width;
	}
	fill(s, 0, s->menu_height - 1, s->width, s->menu_height, 0xb0b0b0);
	s->painting_menu = false;
}
static void native_menu_popup(struct surface *s, uint32_t menu, int root_x,
			      int root_y)
{
	native_menu_close(s);
	int count = 0, width = 160;
	while (native_menu_item(s, menu, count)) {
		struct native_menu_entry *e =
			native_menu_item(s, menu, count++);
		int w = menu_width(e->text) + 30;
		if (w > width)
			width = w;
	}
	if (!count)
		return;
	if (width > 700)
		width = 700;
	if (count > 128)
		count = 128;
	s->popup = XCreateSimpleWindow(display, DefaultRootWindow(display),
				       root_x, root_y, width, count * 22 + 4, 1,
				       0x808080, 0xf0f0f0);
	XSetWindowAttributes attrs = { .override_redirect = True };
	XChangeWindowAttributes(display, s->popup, CWOverrideRedirect, &attrs);
	XSelectInput(display, s->popup,
		     ExposureMask | ButtonPressMask | ButtonReleaseMask |
			     PointerMotionMask | KeyPressMask);
	s->active_menu = menu;
	s->popup_x = root_x;
	s->popup_y = root_y;
	s->popup_width = width;
	s->popup_height = count * 22 + 4;
	XMapRaised(display, s->popup);
	XGrabPointer(display, s->popup, True,
		     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}
static void native_menu_popup_draw(struct surface *s, int selected)
{
	if (!s->popup)
		return;
	struct surface view = { 0 };
	if (resize_surface(&view, s->popup_width, s->popup_height))
		return;
	fill(&view, 0, 0, view.width, view.height, 0xf0f0f0);
	for (unsigned i = 0;; i++) {
		struct native_menu_entry *e =
			native_menu_item(s, s->active_menu, i);
		if (!e)
			break;
		int y = 2 + i * 22;
		if (e->flags & 0x800) {
			fill(&view, 4, y + 10, view.width - 4, y + 11,
			     0xaaaaaa);
			continue;
		}
		if ((int)i == selected)
			fill(&view, 2, y, view.width - 2, y + 22, 0xcce5ff);
		if (e->flags & 8)
			menu_text(&view, 4, y + 3, "✓", 0x202020);
		menu_text(&view, 24, y + 3, e->text,
			  (e->flags & 3) ? 0x888888 : 0x202020);
		const char *shortcut=strchr(e->text,'\t');
		if(shortcut && shortcut[1]) {
			shortcut++;
			int shortcut_width=menu_width(shortcut)-20;
			menu_text(&view,view.width-24-shortcut_width,y+3,shortcut,
				  (e->flags & 3) ? 0x888888 : 0x202020);
		}
		if (e->submenu)
			menu_text(&view, view.width - 18, y + 3, ">", 0x202020);
	}
	XPutImage(display, s->popup, s->gc, view.image, 0, 0, 0, 0, view.width,
		  view.height);
	if (view.face)
		FT_Done_Face(view.face);
	XDestroyImage(view.image);
}
static int native_menu_event(struct surface *s, XEvent *e, struct pg_packet *p)
{
	if (e->xany.window == s->popup) {
		if (e->type == Expose) {
			native_menu_popup_draw(s, -1);
			return 1;
		}
		if (e->type == MotionNotify) {
			native_menu_popup_draw(s, (e->xmotion.y - 2) / 22);
			return 1;
		}
		if (e->type == ButtonPress || e->type == ButtonRelease) {
			int index = (e->xbutton.y - 2) / 22;
			struct native_menu_entry *item =
				e->xbutton.y >= 2 ?
					native_menu_item(s, s->active_menu,
							 index) :
					NULL;
			if (!item || e->xbutton.x < 0 ||
			    e->xbutton.x >= s->popup_width) {
				native_menu_close(s);
				return 1;
			}
			if (e->type == ButtonPress)
				return 1;
			if (item->flags & (3 | 0x800))
				return 1;
			if (item->submenu) {
				int x = s->popup_x + s->popup_width,
				    y = s->popup_y + index * 22;
				native_menu_popup(s, item->submenu, x, y);
				native_menu_popup_draw(s, -1);
				return 1;
			}
			uint32_t command = item->command;
			native_menu_close(s);
			p->a = 0x111;
			p->b = command;
			p->c = p->d = 0;
			return 2;
		}
		return 1;
	}
	if (e->type == KeyPress && s->popup &&
	    XLookupKeysym(&e->xkey, 0) == XK_Escape) {
		native_menu_close(s);
		return 1;
	}
	if (e->type == ButtonPress && s->menu_height &&
	    e->xbutton.y < s->menu_height) {
		int x = 4;
		for (unsigned i = 0;; i++) {
			struct native_menu_entry *item =
				native_menu_item(s, s->root_menu, i);
			if (!item)
				break;
			int width = menu_width(item->text);
			if (e->xbutton.x >= x && e->xbutton.x < x + width) {
				if (item->flags & 3)
					return 1;
				if (item->submenu) {
					Window child;
					int root_x, root_y;
					XTranslateCoordinates(
						display, s->window,
						DefaultRootWindow(display), x,
						s->menu_height, &root_x,
						&root_y, &child);
					native_menu_popup(s, item->submenu,
							  root_x, root_y);
					native_menu_popup_draw(s, -1);
					return 1;
				}
				p->a = 0x111;
				p->b = item->command;
				p->c = p->d = 0;
				return 2;
			}
			x += width;
		}
		return 1;
	}
	if (e->type == ButtonPress && s->popup)
		native_menu_close(s);
	return 0;
}
