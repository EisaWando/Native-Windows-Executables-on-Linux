/* Windows-facing window/message layer; the host has no application pointers. */
#include "protocol.h"
#if __has_include("host_path.h")
#include "host_path.h"
#else
#define PE_GUI_HOST_PATH "./pe_gui_host"
#endif
struct pe_msg {
	HANDLE hwnd;
	DWORD message, pad;
	uintptr_t wparam;
	intptr_t lparam;
	DWORD time;
	int32_t x, y;
	DWORD private;
};
struct pe_paint {
	HANDLE dc;
	BOOL erase;
	struct pe_rect rect;
	BOOL restore, update;
	unsigned char reserved[32];
};
struct pe_create {
	void *parameter;
	HANDLE instance, menu, parent;
	int32_t height, width, y, x;
	int32_t style;
	const uint16_t *name, *class_name;
	DWORD exstyle;
};
_Static_assert(sizeof(struct pe_msg) == 48, "MSG ABI");
_Static_assert(sizeof(struct pe_paint) == 72, "PAINTSTRUCT ABI");
_Static_assert(sizeof(struct pe_create) == 80, "CREATESTRUCTW ABI");
typedef intptr_t(__attribute__((ms_abi)) * pe_wndproc)(HANDLE, DWORD, uintptr_t,
						       intptr_t);
struct pe_ui_message {
	struct pe_msg value;
	struct pe_ui_message *next;
};
struct pe_subclass_frame;
struct pe_ui_call;
struct pe_timer;
struct pe_ui {
	DWORD message_time;
	int32_t message_x, message_y;
	struct pe_ui_call *calls, *active_calls, *outgoing;
	struct pe_subclass_frame *subclass_frame;
	struct pe_hook_frame *hook_frame;
	struct pe_ui *next;
	DWORD tid;
	int fd, child;
	struct pe_ui_message *head, *tail;
	bool failed;
	unsigned char keys[256];
	HANDLE focus, active;
	HANDLE caret, caret_bitmap;
	int caret_x, caret_y, caret_width, caret_height, caret_hidden;
	unsigned com_refs, com_mode, ole_refs;
};
struct pe_property;
struct pe_subclass;
struct pe_control;
struct pe_scrollbars;
struct pe_modal_state { HANDLE hwnd; intptr_t result; bool ended; };
struct pe_point { int32_t x,y; };
static struct pe_point pe_cursor;
struct pe_window {
	struct pe_modal_state *modal;
	unsigned dialog_default_id;
	DWORD dialog_texture_flags;
	HANDLE dialog_font;
	int dialog_base_x, dialog_base_y;
	struct pe_region update;
	struct pe_rect locked_damage;
	bool locked_damage_set;
	struct pe_property *properties;
	struct pe_control *control;
	struct pe_scrollbars *scrollbars;
	void *drop_target;
	struct pe_window *next;
	struct pe_ui *ui;
	struct pe_gui_class *cls;
	HANDLE handle, instance, parent, menu, paint_dc;
	pe_wndproc proc;
	pe_wndproc subclass_base;
	struct pe_subclass *subclasses;
	DWORD style, exstyle;
	int x, y, width, height;
	intptr_t userdata;
	bool visible, invalid, destroying, dead, erase, realized, menu_dirty, reparented;
	uint16_t *theme_app, *theme_ids;
	uint16_t title[256];
	unsigned char extra[4096];
};
static struct pe_ui *pe_uis;
static intptr_t pe_hook_dispatch(int, int, uintptr_t, intptr_t);
static bool pe_hook_input(struct pe_msg *m)
{
	if (m->message >= 0x100 && m->message <= 0x109)
		return pe_hook_dispatch(2,0,m->wparam,m->lparam) != 0;
	if (m->message >= 0x200 && m->message <= 0x20e) {
		struct { int32_t x,y; HANDLE hwnd; DWORD hit; uintptr_t extra; } mouse={m->x,m->y,m->hwnd,1,0};
		return pe_hook_dispatch(7,0,m->message,(intptr_t)&mouse) != 0;
	}
	return false;
}
static struct pe_window *pe_windows;
static struct pe_window *pe_window_lookup(HANDLE hwnd,bool own);
__attribute__((ms_abi)) BOOL InvalidateRect(HANDLE,const struct pe_rect *,BOOL);
static HANDLE pe_locked_window;
static bool pe_update_locked(HANDLE hwnd)
{
    while(hwnd) {
        if(hwnd==pe_locked_window) return true;
        struct pe_window *w=pe_window_lookup(hwnd,false);
        if(!w || !(w->style&0x40000000u)) break;
        hwnd=w->parent;
    }
    return false;
}
__attribute__((ms_abi)) BOOL LockWindowUpdate(HANDLE hwnd)
{
    PE_STATE_GUARD;
    if(hwnd) {
        if(!pe_window_lookup(hwnd,false) || pe_locked_window) return 0;
        pe_locked_window=hwnd;return 1;
    }
    if(!pe_locked_window) return 1;
    pe_locked_window=NULL;
    for(struct pe_window *w=pe_windows;w;w=w->next) {
        if(!w->dead && w->locked_damage_set) {
            struct pe_rect rect=w->locked_damage;w->locked_damage_set=false;
            InvalidateRect(w->handle,&rect,1);
        }
    }
    return 1;
}
static bool pe_coord_origin(struct pe_window *,struct pe_point *);
static bool pe_tree_reorder(struct pe_window *,HANDLE);
static void pe_timer_pump(struct pe_ui *);
static void pe_timer_dequeued(struct pe_ui *,HANDLE,uintptr_t);
static void pe_timer_remove_window(HANDLE);
static void pe_scroll_clear(struct pe_window *);
static void pe_invalidate_full(struct pe_window *);
__attribute__((ms_abi)) BOOL InvalidateRect(HANDLE, const struct pe_rect *, BOOL);
__attribute__((ms_abi)) BOOL ValidateRect(HANDLE, const struct pe_rect *);
__attribute__((ms_abi)) BOOL UpdateWindow(HANDLE);
static void pe_properties_clear(struct pe_window *);
static int pe_window_menu_height(struct pe_window *);
static bool pe_menu_sync(struct pe_window *);
__attribute__((ms_abi)) HANDLE LoadMenuW(HANDLE, const uint16_t *);
__attribute__((ms_abi)) BOOL DestroyMenu(HANDLE);
static void pe_control_clear(struct pe_window *);
static void pe_drop_window_destroy(struct pe_window *);
static const uint16_t *pe_control_caption(struct pe_window *);
static void pe_ui_deliver(struct pe_ui *);
static void pe_ui_cancel(struct pe_ui *);
static void pe_ui_cancel_window(struct pe_window *);
static intptr_t pe_ui_send(HANDLE, DWORD, uintptr_t, intptr_t);
static struct pe_ui *pe_ui_current(void)
{
	PE_STATE_GUARD;
	DWORD tid = GetCurrentThreadId();
	for (struct pe_ui *u = pe_uis; u; u = u->next)
		if (u->tid == tid)
			return u;
	struct pe_ui *u = pe_raw_map(sizeof(*u));
	if (!u) {
		SetLastError(8);
		return NULL;
	}
	u->tid = tid;
	u->fd = -1;
	u->next = pe_uis;
	pe_uis = u;
	return u;
}
static bool pe_ui_receive(struct pe_ui *u, struct pg_packet *p)
{
	struct {
		int fd;
		short events, revents;
	} pollfd = { u->fd, 1, 0 };
	long r;
	do {
		r = raw_syscall3(SYS_poll, (long)&pollfd, 1, 5000);
	} while (r == -4);
	if (r <= 0)
		return false;
	do {
		r = raw_syscall6(SYS_recvfrom, u->fd, (long)p, sizeof(*p), 0x20,
				 0, 0);
	} while (r == -4);
	return r == sizeof(*p) && p->magic == PG_MAGIC;
}
static void pe_ui_disconnect(struct pe_ui *u)
{
	if (u->fd >= 0)
		raw_syscall3(SYS_close, u->fd, 0, 0);
	u->fd = -1;
	u->failed = true;
	if (u->child > 0) {
		raw_syscall3(SYS_kill, u->child, 15, 0);
		while (raw_syscall6(SYS_wait4, u->child, 0, 0, 0, 0, 0) == -4) {
		}
		u->child = 0;
	}
}
static bool pe_ui_connect(struct pe_ui *u)
{
	if (u->fd >= 0)
		return true;
	if (u->failed) {
		SetLastError(31);
		return false;
	}
	const char *path = pe_host_env("BINFMT_PE_GUI_HELPER");
	if (!path || !*path)
		path = PE_GUI_HOST_PATH;
	pe_host_env("DISPLAY");
	char *env[1025];
	unsigned count = 0;
	for (char *p = pe_host_environment; *p && count < 1024;
	     p += cstring_length(p) + 1)
		env[count++] = p;
	env[count] = NULL;
	char *argv[] = { (char *)path, NULL };
	int sockets[2];
	if (raw_syscall4(SYS_socketpair, 1, 5 | 0x80000, 0, (long)sockets) <
	    0) {
		SetLastError(31);
		return false;
	}
	long child = raw_syscall3(SYS_fork, 0, 0, 0);
	if (!child) {
		raw_syscall3(SYS_close, sockets[0], 0, 0);
		if (sockets[1] != 3) {
			raw_syscall3(SYS_dup3, sockets[1], 3, 0);
			raw_syscall3(SYS_close, sockets[1], 0, 0);
		} else
			raw_syscall3(SYS_fcntl, 3, 2, 0);
		raw_syscall3(SYS_close_range, 4, UINT32_MAX, 0);
		raw_syscall3(SYS_execve, (long)path, (long)argv, (long)env);
		raw_syscall3(SYS_exit, 127, 0, 0);
		__builtin_unreachable();
	}
	raw_syscall3(SYS_close, sockets[1], 0, 0);
	if (child < 0) {
		raw_syscall3(SYS_close, sockets[0], 0, 0);
		SetLastError(8);
		return false;
	}
	u->fd = sockets[0];
	u->child = child;
	struct pg_packet hello;
	if (!pe_ui_receive(u, &hello) || hello.op != PG_HELLO || hello.status) {
		pe_ui_disconnect(u);
		SetLastError(126);
		return false;
	}
	return true;
}
static bool pe_ui_rpc(struct pe_ui *u, struct pg_packet *p)
{
	if (u->tid != GetCurrentThreadId()) {
		SetLastError(5);
		return false;
	}
	if (!pe_ui_connect(u))
		return false;
	p->magic = PG_MAGIC;
	uint32_t op = p->op;
	uint64_t id = p->id;
	long n;
	/* MSG_DONTWAIT prevents a dead helper from blocking the bridge sender. */
	do {
		n = raw_syscall6(SYS_sendto, u->fd, (long)p, sizeof(*p), 0x4040,
				 0, 0);
	} while (n == -4);
	if (n != sizeof(*p) || !pe_ui_receive(u, p) ||
	    p->op != (op == PG_POLL ? PG_EVENT : op) ||
	    (op != PG_POLL && p->id != id)) {
		pe_ui_disconnect(u);
		SetLastError(31);
		return false;
	}
	if (p->status) {
		SetLastError(p->status);
		return false;
	}
	return true;
}
static struct pe_window *pe_window_lookup(HANDLE hwnd, bool own)
{
	PE_STATE_GUARD;
	struct pe_gui_object *o = pe_gui_lookup(hwnd, PE_GUI_WINDOW);
	if (!o || !o->window || o->window->dead) {
		SetLastError(1400);
		return NULL;
	}
	if (own && o->window->ui->tid != GetCurrentThreadId()) {
		SetLastError(5);
		return NULL;
	}
	return o->window;
}
static bool pe_ui_post(struct pe_ui *u, HANDLE hwnd, DWORD msg, uintptr_t wp,
		       intptr_t lp, DWORD character)
{
	PE_STATE_GUARD;
	if (!u->tid) {
		SetLastError(1444);
		return false;
	}
	struct pe_ui_message *m = pe_raw_map(sizeof(*m));
	if (!m) {
		SetLastError(8);
		return false;
	}
	m->value = (struct pe_msg){ .hwnd = hwnd,
				    .message = msg,
				    .wparam = wp,
				    .lparam = lp,
				    .time = GetTickCount(),
				    .x = pe_cursor.x, .y = pe_cursor.y,
				    .private = character };
	if (u->tail)
		u->tail->next = m;
	else
		u->head = m;
	u->tail = m;
	return true;
}
__attribute__((ms_abi)) BOOL PostMessageW(HANDLE hwnd, DWORD msg, uintptr_t wp,
					  intptr_t lp)
{
	struct pe_ui *u;
	if (!hwnd)
		u = pe_ui_current();
	else {
		struct pe_window *w = pe_window_lookup(hwnd, false);
		if (!w)
			return 0;
		u = w->ui;
	}
	return u && pe_ui_post(u, hwnd, msg, wp, lp, 0);
}
__attribute__((ms_abi)) BOOL PostThreadMessageW(DWORD tid, DWORD msg,
						uintptr_t wp, intptr_t lp)
{
	PE_STATE_GUARD;
	for (struct pe_ui *u = pe_uis; u; u = u->next)
		if (u->tid == tid)
			return pe_ui_post(u, NULL, msg, wp, lp, 0);
	SetLastError(1444);
	return 0;
}
__attribute__((ms_abi)) void PostQuitMessage(int code)
{
	struct pe_ui *u = pe_ui_current();
	if (u)
		pe_ui_post(u, NULL, 0x12, (uint32_t)code, 0, 0);
}
__attribute__((ms_abi)) BOOL DestroyWindow(HANDLE hwnd);
__attribute__((ms_abi)) BOOL IsWindow(HANDLE hwnd);
__attribute__((ms_abi)) HANDLE BeginPaint(HANDLE, struct pe_paint *);
__attribute__((ms_abi)) BOOL EndPaint(HANDLE, const struct pe_paint *);
__attribute__((ms_abi)) int FillRect(HANDLE, const struct pe_rect *, HANDLE);
__attribute__((ms_abi)) intptr_t DefWindowProcW(HANDLE hwnd, DWORD msg,
						uintptr_t wp, intptr_t lp)
{
	(void)wp;
	(void)lp;
	if (msg == 0x81)
		return 1;
	if (msg == 0x10) {
		DestroyWindow(hwnd);
		return 0;
	}

	if (msg == 0x83 && lp) {
		struct pe_window *w = pe_window_lookup(hwnd, true);
		if (w)
			((struct pe_rect *)lp)->top += pe_window_menu_height(w);
		return 0;
	}
	if (msg == 0x14) {
		struct pe_window *w = pe_window_lookup(hwnd, true);
		if (w && w->cls->data.background) {
			struct pe_rect r = { 0, 0, w->width, w->height };
			return FillRect((HANDLE)wp, &r,
					w->cls->data.background) != 0;
		}
	}
	if (msg == 0xf) {
		struct pe_paint paint;
		if (BeginPaint(hwnd, &paint))
			EndPaint(hwnd, &paint);
	}
	return 0;
}
__attribute__((ms_abi)) intptr_t CallWindowProcW(pe_wndproc proc, HANDLE hwnd,
						 DWORD msg, uintptr_t wp,
						 intptr_t lp)
{
	if (!proc) {
		SetLastError(87);
		return 0;
	}
	return proc(hwnd, msg, wp, lp);
}
__attribute__((ms_abi)) intptr_t SendMessageW(HANDLE hwnd, DWORD msg,
					      uintptr_t wp, intptr_t lp)
{
	return pe_ui_send(hwnd, msg, wp, lp);
}
__attribute__((ms_abi)) BOOL ShowWindow(HANDLE, int);
static bool pe_window_realize(struct pe_window *w)
{
	if (w->realized)
		return true;
	if (w->cls->builtin || w->parent == (HANDLE)(intptr_t)-3 ||
	    (w->style & 0x40000000u))
		return true;
	if (w->width > PG_MAX_DIM || w->height > PG_MAX_DIM) {
		SetLastError(8);
		return false;
	}
	struct pg_packet p = { .op = PG_CREATE,
			       .id = (uintptr_t)w->handle,
			       .a = w->x,
			       .b = w->y,
			       .c = w->width > 0 ? w->width : 1,
			       .d = w->height > 0 ? w->height : 1 };
	if (!WideCharToMultiByte(65001, 0, w->title, -1, p.text, PG_TEXT, NULL,
				 NULL))
		return false;
	if (!pe_ui_rpc(w->ui, &p))
		return false;
	w->realized = true;
	if (w->menu && !pe_menu_sync(w))
		return false;
	return true;
}
__attribute__((ms_abi)) HANDLE CreateWindowExW(DWORD ex, const uint16_t *cls,
					       const uint16_t *title,
					       DWORD style, int x, int y,
					       int width, int height,
					       HANDLE parent, HANDLE menu,
					       HANDLE instance, void *parameter)
{
	/* Every style bit is retained. Backend support is separate from HWND state. */
	if (parent && parent != (HANDLE)(intptr_t)-3 &&
	    !pe_window_lookup(parent, true))
		return NULL;
	if (!cls) {
		SetLastError(87);
		return NULL;
	}
	if (x == INT32_MIN)
		x = 80;
	if (y == INT32_MIN)
		y = 80;
	if (width == INT32_MIN)
		width = 640;
	if (height == INT32_MIN)
		height = 400;
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return NULL;
	struct pe_window *w = pe_raw_map(sizeof(*w));
	if (!w) {
		SetLastError(8);
		return NULL;
	}
	{
		PE_STATE_GUARD;
		struct pe_gui_class *c = pe_class_lookup(instance, cls);
		if (!c) {
			SetLastError(1407);
			raw_syscall3(SYS_munmap, (long)w, sizeof(*w), 0);
			return NULL;
		}
		struct pe_gui_object *o = pe_gui_new(PE_GUI_WINDOW);
		if (!o) {
			raw_syscall3(SYS_munmap, (long)w, sizeof(*w), 0);
			return NULL;
		}
		w->ui = u;
		w->cls = c;
		w->proc = (pe_wndproc)c->data.wndproc;
		w->instance = instance;
		w->style = style;
		w->exstyle = ex;
		w->parent = parent;
		w->menu = menu;
		w->x = x;
		w->y = y;
		w->width = width;
		w->height = height;
		w->handle = pe_gui_handle(o);
		o->window = w;
		c->windows++;
		if (title && !pe_copy_name(w->title, 256, title)) {
			c->windows--;
			pe_gui_drop(o);
			raw_syscall3(SYS_munmap, (long)w, sizeof(*w), 0);
			return NULL;
		}
		w->next = pe_windows;
		pe_windows = w;
		pe_tree_reorder(w,NULL);
	}
	if (!w->menu && !(w->style & 0x40000000u) && w->cls->data.menu_name) {
		w->menu = LoadMenuW(instance, w->cls->data.menu_name);
		if (!w->menu) {
			PE_STATE_GUARD;
			w->dead = true;
			w->cls->windows--;
			pe_gui_drop(pe_gui_lookup(w->handle, PE_GUI_WINDOW));
			return NULL;
		}
	}
	struct pe_create cs = { parameter,	instance, menu, parent,
				height,		width,	  y,	x,
				(int32_t)style, title,	  cls,	ex };
	struct { struct pe_create *cs; HANDLE after; } cbt = { &cs, NULL };
	if (pe_hook_dispatch(5,3,(uintptr_t)w->handle,(intptr_t)&cbt)) {
		PE_STATE_GUARD;
		w->dead = true; w->cls->windows--;
		if (w->menu && !(w->style & 0x40000000u)) DestroyMenu(w->menu);
		pe_gui_drop(pe_gui_lookup(w->handle, PE_GUI_WINDOW));
		return NULL;
	}
	intptr_t nc = w->proc(w->handle, 0x81, 0, (intptr_t)&cs);
	if (w->dead)
		return NULL;
	if (!nc) {
		/* WM_NCCREATE rejection has no WM_DESTROY, but does get NC teardown. */
		w->destroying = true;
		w->proc(w->handle, 0x82, 0, 0);
		if (w->realized) {
			struct pg_packet p = { .op = PG_DESTROY,
					       .id = (uintptr_t)w->handle };
			pe_ui_rpc(u, &p);
		}
		PE_STATE_GUARD;
		w->dead = true;
		w->cls->windows--;
		if (w->menu && !(w->style & 0x40000000u))
			DestroyMenu(w->menu);
		if (w->theme_app) raw_syscall3(SYS_munmap,(long)w->theme_app,(lstrlenW(w->theme_app)+1)*2,0);
	if (w->theme_ids) raw_syscall3(SYS_munmap,(long)w->theme_ids,(lstrlenW(w->theme_ids)+1)*2,0);
	w->theme_app = w->theme_ids = NULL;
	pe_region_clear(&w->update);
	pe_scroll_clear(w);
	pe_properties_clear(w);
		struct pe_gui_object *o =
			pe_gui_lookup(w->handle, PE_GUI_WINDOW);
		if (o)
			pe_gui_drop(o);
		return NULL;
	}
	if (w->dead)
		return NULL;
	if (w->proc(w->handle, 1, 0, (intptr_t)&cs) == -1) {
		if (!w->dead)
			DestroyWindow(w->handle);
		return NULL;
	}
	if (w->dead)
		return NULL;
    if ((w->style & 0x40000000u) && !(w->exstyle & 4) && w->parent) {
        HANDLE p=w->parent;
        while (p && IsWindow(p)) {
            SendMessageW(p,0x210,1|((uintptr_t)(uint16_t)(uintptr_t)w->menu<<16),(intptr_t)w->handle);
            struct pe_window *ancestor=pe_window_lookup(p,false);
            p=ancestor && (ancestor->style&0x40000000u) ? ancestor->parent : NULL;
        }
        if (w->dead) return NULL;
    }
	if (style & 0x10000000u) {
		SetLastError(0);
		ShowWindow(w->handle, 1);
		if (!w->visible) {
			DWORD error = GetLastError();
			DestroyWindow(w->handle);
			SetLastError(error);
			return NULL;
		}
	}
	return w->handle;
}
__attribute__((ms_abi)) BOOL DestroyWindow(HANDLE hwnd)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w || w->destroying)
		return 0;
	if (pe_hook_dispatch(5,4,(uintptr_t)hwnd,0)) return 0;
	w->destroying = true;
    if ((w->style & 0x40000000u) && !(w->exstyle & 4) && w->parent) {
        HANDLE p=w->parent;
        while (p && IsWindow(p)) {
            SendMessageW(p,0x210,2|((uintptr_t)(uint16_t)(uintptr_t)w->menu<<16),(intptr_t)hwnd);
            struct pe_window *ancestor=pe_window_lookup(p,false);
            p=ancestor && (ancestor->style&0x40000000u) ? ancestor->parent : NULL;
        }
    }
	w->proc(hwnd, 2, 0, 0);
	struct pe_window *first;
	{
		PE_STATE_GUARD;
		first = pe_windows;
	}
	for (struct pe_window *child = first; child; child = child->next)
		if (!child->dead && child->parent == hwnd &&
		    (child->style & 0x40000000u))
			DestroyWindow(child->handle);
	w->proc(hwnd, 0x82, 0, 0);
	pe_timer_remove_window(hwnd);
	pe_drop_window_destroy(w);
	if (w->paint_dc) {
		ReleaseDC(hwnd, w->paint_dc);
		w->paint_dc = NULL;
	}
	bool ok = true;
	if (w->realized) {
		struct pg_packet p = { .op = PG_DESTROY,
				       .id = (uintptr_t)hwnd };
		ok = pe_ui_rpc(w->ui, &p);
	}
	if (w->ui->focus == hwnd)
		w->ui->focus = NULL;
	if (w->ui->active == hwnd)
		w->ui->active = NULL;
	PE_STATE_GUARD;
	if (w->ui->caret == hwnd) {
		w->ui->caret = w->ui->caret_bitmap = NULL;
		w->ui->caret_hidden = 0;
	}
	w->dead = true;
	w->visible = false;
	w->cls->windows--;
	if (w->theme_app) raw_syscall3(SYS_munmap,(long)w->theme_app,(lstrlenW(w->theme_app)+1)*2,0);
	if (w->theme_ids) raw_syscall3(SYS_munmap,(long)w->theme_ids,(lstrlenW(w->theme_ids)+1)*2,0);
	w->theme_app = w->theme_ids = NULL;
	pe_region_clear(&w->update);
	pe_scroll_clear(w);
	pe_properties_clear(w);
	pe_control_clear(w);
	if (w->dialog_font) { DeleteObject(w->dialog_font); w->dialog_font = NULL; }
	if (w->menu && !(w->style & 0x40000000u))
		DestroyMenu(w->menu);
	pe_ui_cancel_window(w);
	struct pe_gui_object *o = pe_gui_lookup(hwnd, PE_GUI_WINDOW);
	if (o)
		pe_gui_drop(o);
	return ok;
}
__attribute__((ms_abi)) BOOL IsWindow(HANDLE hwnd)
{
	DWORD saved = GetLastError();
	bool ok = pe_window_lookup(hwnd, false) != NULL;
	SetLastError(saved);
	return ok;
}
__attribute__((ms_abi)) BOOL IsWindowVisible(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	for (unsigned depth = 0; depth < 128; depth++) {
		if (!w->visible)
			return 0;
		if (!(w->style & 0x40000000u) || !w->parent)
			return 1;
		w = pe_window_lookup(w->parent, false);
		if (!w)
			return 0;
	}
	return 0;
}
__attribute__((ms_abi)) BOOL IsWindowUnicode(HANDLE hwnd)
{
	return IsWindow(hwnd);
}
__attribute__((ms_abi)) DWORD GetWindowThreadProcessId(HANDLE hwnd, DWORD *pid)
{
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	if (pid)
		*pid = GetCurrentProcessId();
	return w->ui->tid;
}
__attribute__((ms_abi)) BOOL ShowWindow(HANDLE hwnd, int command)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	if (command < 0 || command > 11) {
		SetLastError(87);
		return 0;
	}
	if ((command==2 || command==3 || command==6 || command==7 || command==9 || command==11) &&
	    pe_hook_dispatch(5,1,(uintptr_t)hwnd,command)) return 0;
	BOOL previous = w->visible;
	bool visible = command != 0;
	if (visible && !w->realized && !w->cls->builtin && !(w->style & 0x40000000u) && w->parent != (HANDLE)(intptr_t)-3) {
		raw_puts("[BRIDGE] GUI visible top-level request hwnd=");
		raw_hex((uintptr_t)hwnd); raw_puts("\n");
	}
	if (visible && !pe_window_realize(w))
		return 0;
	if (w->realized) {
		struct pg_packet p = { .op = PG_SHOW,
				       .id = (uintptr_t)hwnd,
				       .a = visible };
		if (!pe_ui_rpc(w->ui, &p))
			return 0;
	}
	{
		PE_STATE_GUARD;
		w->visible = visible;
		w->style = visible ? w->style | 0x10000000u :
				     w->style & ~0x10000000u;
		if (visible) pe_invalidate_full(w);
		w->erase |= visible;
	}
	w->proc(hwnd, 0x18, visible, 0);
	if (!w->dead && visible)
		w->proc(hwnd, 5,
			(command == 2 || command == 6 || command == 7) ? 1 :
			command == 3				       ? 2 :
									 0,
			(uint16_t)w->width |
				((uint32_t)(uint16_t)(w->height - pe_window_menu_height(w)) << 16));
	return previous;
}
__attribute__((ms_abi)) BOOL GetClientRect(HANDLE hwnd, struct pe_rect *r)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w || !r) {
		if (!r)
			SetLastError(87);
		return 0;
	}
	*r = (struct pe_rect){ 0, 0, w->width,
			       w->height - pe_window_menu_height(w) };
	return 1;
}
__attribute__((ms_abi)) BOOL GetWindowRect(HANDLE hwnd, struct pe_rect *r)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w || !r)
		return 0;
	struct pe_point origin;
	if (!pe_coord_origin(w,&origin)) return 0;
	int32_t top=(uint32_t)origin.y-(uint32_t)pe_window_menu_height(w);
	*r = (struct pe_rect){origin.x,top,(uint32_t)origin.x+(uint32_t)w->width,
		(uint32_t)top+(uint32_t)w->height};
	return 1;
}
__attribute__((ms_abi)) BOOL SetWindowTextW(HANDLE hwnd, const uint16_t *title)
{
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w || !title)
		return 0;
	if (w->cls->builtin)
		return SendMessageW(hwnd, 0xc, 0, (intptr_t)title) != 0;
	uint16_t copy[256];
	if (!pe_copy_name(copy, 256, title))
		return 0;
	struct pg_packet p = { .op = PG_TITLE, .id = (uintptr_t)hwnd };
	WideCharToMultiByte(65001, 0, copy, -1, p.text, PG_TEXT, NULL, NULL);
	if (w->realized && !pe_ui_rpc(w->ui, &p))
		return 0;
	{
		PE_STATE_GUARD;
		memcpy(w->title, copy, (lstrlenW(copy) + 1) * 2);
	}
	return 1;
}
__attribute__((ms_abi)) int GetWindowTextW(HANDLE hwnd, uint16_t *out, int cap)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w || !out || cap < 1)
		return 0;
	int n = lstrlenW(pe_control_caption(w));
	if (n >= cap)
		n = cap - 1;
	memcpy(out, pe_control_caption(w), n * 2);
	out[n] = 0;
	return n;
}
__attribute__((ms_abi)) int GetWindowTextLengthW(HANDLE hwnd)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	return w ? lstrlenW(pe_control_caption(w)) : 0;
}
__attribute__((ms_abi)) intptr_t GetWindowLongPtrW(HANDLE hwnd, int index)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, false);
	if (!w)
		return 0;
	switch (index) {
	case -21:
		return w->userdata;
	case -4:
		return (intptr_t)w->proc;
	case -6:
		return (intptr_t)w->instance;
	case -16:
		return w->style;
	case -20:
		return w->exstyle;
	case -12:
		return (intptr_t)w->menu;
	case -8:
		return (intptr_t)w->parent;
	}
	if (index >= 0 && index <= w->cls->data.window_extra - 8) {
		intptr_t v;
		memcpy(&v, w->extra + index, 8);
		return v;
	}
	SetLastError(1413);
	return 0;
}
__attribute__((ms_abi)) intptr_t SetWindowLongPtrW(HANDLE hwnd, int index,
						   intptr_t value)
{
	PE_STATE_GUARD;
	struct pe_window *w = pe_window_lookup(hwnd, true);
	if (!w)
		return 0;
	intptr_t old;
	if (index == -16) {
		old = w->style;
		w->style = (DWORD)value;
		w->visible = !!(w->style & 0x10000000u);
		return old;
	}
	if (index == -20) {
		old = w->exstyle;
		w->exstyle = (DWORD)value;
		return old;
	}
	if (index == -12) {
		old = (intptr_t)w->menu;
		w->menu = (HANDLE)value;
		return old;
	}
	if (index == -21) {
		old = w->userdata;
		w->userdata = value;
		return old;
	}
	if (index == -4 && value) {
		old = (intptr_t)w->proc;
		w->proc = (pe_wndproc)value;
		return old;
	}
	if (index >= 0 && index <= w->cls->data.window_extra - 8) {
		memcpy(&old, w->extra + index, 8);
		memcpy(w->extra + index, &value, 8);
		return old;
	}
	SetLastError(1413);
	return 0;
}
static bool pe_ui_pump(struct pe_ui *u, int wait)
{
	for (struct pe_window *v = pe_windows; v; v = v->next)
		if (v->ui == u && !v->dead && v->realized && v->menu_dirty)
			if (!pe_menu_sync(v))
				return false;

	if (u->fd < 0) {
		if (u->failed) {
			SetLastError(31);
			return false;
		}
		if (wait)
			Sleep(wait);
		return true;
	}
	struct pg_packet p = { .op = PG_POLL, .a = wait };
	if (!pe_ui_rpc(u, &p))
		return false;
	if (!p.id || !p.a)
		return true;
	struct pe_window *w = pe_window_lookup((HANDLE)(uintptr_t)p.id, true);
	if (!w)
		return true;
	if (p.a >= 0x100 && p.a <= 0x20e && (w->style & 0x08000000u))
		return true;
	if ((p.a == 0x100 || p.a == 0x101) && p.b >= 0 && p.b < 256)
		u->keys[p.b] = p.a == 0x100;
	if (p.a >= 0x200 && p.a <= 0x20e) {
		{ PE_STATE_GUARD; pe_cursor.x=(int32_t)p.color; pe_cursor.y=(int32_t)p.background; }
		p.d -= pe_window_menu_height(w);
	}
	if (p.a == 7)
		u->focus = w->handle;
	if (p.a == 8 && u->focus == w->handle)
		u->focus = NULL;
	if (p.a == 0xf) {
		PE_STATE_GUARD;
		pe_invalidate_full(w);
		w->erase = true;
		return true;
	}
	if (p.a == 5) {
		PE_STATE_GUARD;
		w->width = p.c;
		w->height = p.d;
		pe_invalidate_full(w);
		w->erase = true;
	}
	if (p.a == 5)
		p.d -= pe_window_menu_height(w);
	intptr_t lp = (p.a == 0x100 || p.a == 0x101) ?
			      (uint32_t)p.c :
			      (uint16_t)p.c | ((uint32_t)(uint16_t)p.d << 16);
	return pe_ui_post(u, w->handle, p.a, p.b, lp, p.flags);
}
static int pe_ui_get(struct pe_msg *out, HANDLE hwnd, DWORD min, DWORD max,
		     DWORD flags, bool block)
{
	if (!out || (max && min > max) || (flags & ~1u)) {
		SetLastError(87);
		return -1;
	}
	struct pe_ui *u = pe_ui_current();
	if (!u)
		return -1;
	if (hwnd && hwnd != (HANDLE)(intptr_t)-1 &&
	    !pe_window_lookup(hwnd, true))
		return -1;
	bool polled = false;
	for (;;) {
pe_hook_retry:
		pe_ui_deliver(u);
		pe_timer_pump(u);
		{
			PE_STATE_GUARD;
			struct pe_ui_message **link = &u->head,
					     *previous = NULL;
			while (*link) {
				struct pe_ui_message *m = *link;
				bool matches =
					m->value.message == 0x12 ||
					((!hwnd || m->value.hwnd == hwnd ||
					  (hwnd == (HANDLE)(intptr_t)-1 &&
					   !m->value.hwnd)) &&
					 m->value.message >= min &&
					 (!max || m->value.message <= max));
				if (matches) {
					*out = m->value;
					u->message_x=out->x; u->message_y=out->y; u->message_time=out->time;
					if (flags & 1) {
                        if (out->message==0x113) pe_timer_dequeued(u,out->hwnd,out->wparam);
						*link = m->next;
						if (u->tail == m)
							u->tail = previous;
						raw_syscall3(SYS_munmap,
							     (long)m,
							     sizeof(*m), 0);
					}
					pe_state_leave(&pe_guard);
					if (pe_hook_input(out)) { if (flags & 1) goto pe_hook_retry; return 2; }
					pe_hook_dispatch(3,0,flags & 1,(intptr_t)out);
					return out->message == 0x12 ? 0 : 1;
				}
				previous = m;
				link = &m->next;
			}
            if (hwnd != (HANDLE)(intptr_t)-1 && min <= 0xf && (!max || max >= 0xf)) {
                struct pe_window *paint=NULL;
                for (struct pe_window *w=pe_windows;w;w=w->next)
                    if (!w->dead && w->ui==u && w->realized && w->visible && w->invalid && (!hwnd || w->handle==hwnd)) paint=w;
                if (paint) {
                    *out=(struct pe_msg){.hwnd=paint->handle,.message=0xf,.time=GetTickCount(),.x=pe_cursor.x,.y=pe_cursor.y};
                    u->message_x=out->x;u->message_y=out->y;u->message_time=out->time;
                    pe_state_leave(&pe_guard);
                    pe_hook_dispatch(3,0,flags & 1,(intptr_t)out);
                    return 1;
                }
            }
		}
		if (!block && polled)
			return 2;
		if (!pe_ui_pump(u, block ? 16 : 0))
			return -1;
		polled = true;
	}
}
__attribute__((ms_abi)) BOOL GetMessageW(struct pe_msg *out, HANDLE hwnd,
					 DWORD min, DWORD max)
{
	return pe_ui_get(out, hwnd, min, max, 1, true);
}
__attribute__((ms_abi)) BOOL PeekMessageW(struct pe_msg *out, HANDLE hwnd,
					  DWORD min, DWORD max, DWORD flags)
{
	int r = pe_ui_get(out, hwnd, min, max, flags, false);
	return r == 0 || r == 1;
}
__attribute__((ms_abi)) BOOL TranslateMessage(const struct pe_msg *m)
{
	if (!m)
		return 0;
	if (m->message == 0x100 && m->private) {
		PostMessageW(m->hwnd, 0x102, m->private, m->lparam);
		return 1;
	}
	return 0;
}
__attribute__((ms_abi)) intptr_t DispatchMessageW(const struct pe_msg *m)
{
	if (!m || m->message == 0x12) return 0;
    if (m->message==0x113 && m->lparam) {
        typedef void (__attribute__((ms_abi)) *timer_callback)(HANDLE,DWORD,uintptr_t,DWORD);
        ((timer_callback)m->lparam)(m->hwnd,m->message,m->wparam,GetTickCount());
        return 0;
    }
    if (!m->hwnd) return 0;
	struct pe_window *w = pe_window_lookup(m->hwnd, true);
	return w ? w->proc(m->hwnd, m->message, m->wparam, m->lparam) : 0;
}
__attribute__((ms_abi)) int ReleaseDC(HANDLE hwnd, HANDLE dc)
{
	{
		PE_STATE_GUARD;
		struct pe_gui_object *o = pe_gui_lookup(dc, PE_GDI_DC);
		if (!o || o->u.dc.owner != hwnd) {
			SetLastError(6);
			return 0;
		}
	}
	BOOL ok = 1;
	if (hwnd) {
		struct pe_window *w = pe_window_lookup(hwnd, true);
		if (!w)
			return 0;
		struct pg_packet p = { .op = PG_PRESENT,
				       .id = (uintptr_t)hwnd };
		if (w->realized)
			ok = pe_ui_rpc(w->ui, &p);
	}
	{
		PE_STATE_GUARD;
		struct pe_gui_object *o = pe_gui_lookup(dc, PE_GDI_DC);
		if (o)
			pe_drop_dc(o);
	}
	return ok;
}
static uint32_t pe_color_rgb(DWORD c)
{
	return ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255);
}
static struct pe_window *pe_drawing_window(HANDLE dc,
					   struct pe_gui_object **object)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return NULL;
	if (d->type != PE_GDI_DC || !d->u.dc.owner) {
		SetLastError(50);
		return NULL;
	}
	struct pe_window *w = pe_window_lookup(d->u.dc.owner, true);
	if (object)
		*object = d;
	return w;
}
static BOOL pe_draw(HANDLE dc, struct pg_packet *p)
{
	struct pe_window *w = pe_drawing_window(dc, NULL);
	if (!w)
		return 0;
	{
		PE_STATE_GUARD;
		if(pe_update_locked(w->handle)) {
			struct pe_rect r;
			if(p->op==PG_FILL || p->op==PG_LINE) r=(struct pe_rect){p->a,p->b,p->c,p->d};
			else if(p->op==PG_PIXEL) r=(struct pe_rect){p->a,p->b,p->a+1,p->b+1};
			else r=(struct pe_rect){0,0,w->width,w->height};
			if(!w->locked_damage_set) { w->locked_damage=r;w->locked_damage_set=true; }
			else { if(r.left<w->locked_damage.left)w->locked_damage.left=r.left;if(r.top<w->locked_damage.top)w->locked_damage.top=r.top;
			       if(r.right>w->locked_damage.right)w->locked_damage.right=r.right;if(r.bottom>w->locked_damage.bottom)w->locked_damage.bottom=r.bottom; }
			return 1;
		}
	}
	if (!w->realized) {
		SetLastError(50);
		return 0;
	}
	int origin = pe_window_menu_height(w);
	p->b += origin;
	if (p->op == PG_FILL || p->op == PG_LINE)
		p->d += origin;
	p->id = (uintptr_t)w->handle;
    struct pe_region clip = {0};
    bool active;
    {
        PE_STATE_GUARD;
        struct pe_gui_object *d = pe_dc_lookup(dc);
        if (!d) return 0;
        active = d->clip_active;
        if (active && !pe_region_copy(&clip, &d->region)) return 0;
    }
    struct pg_packet set = { .op=PG_CLIP, .id=(uintptr_t)w->handle, .flags=active ? 1 : 0 };
    BOOL ok = pe_ui_rpc(w->ui, &set);
    for (size_t i=0; ok && i<clip.count; i++) {
        struct pe_rect r=pe_rect_intersect(clip.rects[i], (struct pe_rect){0,0,w->width,w->height-origin});
        if (!pe_rect_nonempty(r)) continue;
        set=(struct pg_packet){.op=PG_CLIP,.id=(uintptr_t)w->handle,.flags=2,
            .a=r.left,.b=r.top+origin,.c=r.right,.d=r.bottom+origin};
        ok=pe_ui_rpc(w->ui,&set);
    }
    pe_region_clear(&clip);
    return ok && pe_ui_rpc(w->ui, p);
}
__attribute__((ms_abi)) int FillRect(HANDLE dc, const struct pe_rect *r,
				     HANDLE brush)
{
	if (!r) {
		SetLastError(87);
		return 0;
	}
	DWORD color;
	{
		PE_STATE_GUARD;
		if ((uintptr_t)brush <= 31 && brush)
			color = GetSysColor((uintptr_t)brush - 1);
		else {
			struct pe_gui_object *b =
				pe_gui_lookup(brush, PE_GDI_BRUSH);
			if (!b)
				return 0;
			if (b->u.brush.style == 1)
				return 1;
			color = b->u.brush.color;
		}
	}
    {
        PE_STATE_GUARD;
        struct pe_gui_object *d=pe_dc_lookup(dc);
        if (!d) return 0;
        if (d->type==PE_GDI_MEMDC) {
            struct pe_gui_object *b=pe_gui_lookup(d->u.dc.bitmap,PE_GDI_BITMAP);
            struct pe_raster *dst=b?pe_raster_resource(b):NULL;
            if (!dst) return 0;
            int l=r->left<0?0:r->left,t=r->top<0?0:r->top;
            int right=r->right>dst->width?dst->width:r->right;
            int bottom=r->bottom>dst->height?dst->height:r->bottom;
            uint32_t argb=0xff000000u | pe_color_rgb(color);
            for (int y=t;y<bottom;y++) for (int x=l;x<right;x++)
                if (!d->clip_active || pe_region_contains(&d->region,x,y)) pe_raster_set(dst,x,y,argb);
            return 1;
        }
    }
	struct pg_packet p = { .op = PG_FILL,
			       .a = r->left,
			       .b = r->top,
			       .c = r->right,
			       .d = r->bottom,
			       .color = pe_color_rgb(color) };
	return pe_draw(dc, &p);
}
__attribute__((ms_abi)) DWORD SetTextColor(HANDLE dc, DWORD color)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return UINT32_MAX;
	DWORD old = d->u.dc.text_color;
	d->u.dc.text_color = color;
	return old;
}
__attribute__((ms_abi)) DWORD SetBkColor(HANDLE dc, DWORD color)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return UINT32_MAX;
	DWORD old = d->u.dc.background;
	d->u.dc.background = color;
	return old;
}
__attribute__((ms_abi)) int SetBkMode(HANDLE dc, int mode)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return 0;
	if (mode != 1 && mode != 2) {
		SetLastError(87);
		return 0;
	}
	int old = d->u.dc.background_mode;
	d->u.dc.background_mode = mode;
	return old;
}
__attribute__((ms_abi)) BOOL TextOutW(HANDLE dc, int x, int y,
				      const uint16_t *text, int count)
{
	if (count < 0 || (!text && count)) {
		SetLastError(87);
		return 0;
	}
	if (!count)
		return 1;
	struct pg_packet p = { .op = PG_TEXT_OUT, .a = x, .b = y };
	struct pe_gui_object *d;
	struct pe_window *w = pe_drawing_window(dc, &d);
	if (!w)
		return 0;
	struct pe_logfont f;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *font =
			pe_gui_lookup(d->u.dc.font, PE_GDI_FONT);
		if (!font)
			return 0;
		f = font->u.font;
		p.color = pe_color_rgb(d->u.dc.text_color);
		p.background = pe_color_rgb(d->u.dc.background);
		p.flags = d->u.dc.background_mode;
	}
	int len = WideCharToMultiByte(65001, 0, text, count, p.text,
				      PG_TEXT - 130, NULL, NULL);
	if (!len)
		return 0;
	p.length = len;
	p.text[len] = 0;
	if (!WideCharToMultiByte(65001, 0, f.face, -1, p.text + len + 1,
				 PG_TEXT - len - 1, NULL, NULL))
		return 0;
	p.c = f.height;
	p.id = (uintptr_t)w->handle;
	p.b += pe_window_menu_height(w);
	return pe_ui_rpc(w->ui, &p);
}
__attribute__((ms_abi)) DWORD GetPixel(HANDLE dc, int x, int y)
{
	{
		PE_STATE_GUARD;
		struct pe_gui_object *d = pe_dc_lookup(dc);
		if (d && d->type == PE_GDI_MEMDC) {
			struct pe_gui_object *b =
				pe_gui_lookup(d->u.dc.bitmap, PE_GDI_BITMAP);
			if (!b)
				return UINT32_MAX;
			struct pe_raster *r = pe_raster_resource(b);
			if (!r || x < 0 || y < 0 || x >= r->width ||
			    y >= r->height)
				return UINT32_MAX;
			uint32_t argb = pe_raster_get(r, x, y);
			return ((argb >> 16) & 0xff) | (argb & 0xff00u) |
			       ((argb & 0xff) << 16);
		}
	}
	struct pg_packet p = { .op = PG_GET_PIXEL, .a = x, .b = y };
	return pe_draw(dc, &p) ? pe_color_rgb(p.color) : UINT32_MAX;
}
__attribute__((ms_abi)) DWORD SetPixel(HANDLE dc, int x, int y, DWORD color)
{
	{
		PE_STATE_GUARD;
		struct pe_gui_object *d = pe_dc_lookup(dc);
		if (d && d->type == PE_GDI_MEMDC) {
			struct pe_gui_object *b =
				pe_gui_lookup(d->u.dc.bitmap, PE_GDI_BITMAP);
			if (!b)
				return UINT32_MAX;
			struct pe_raster *r = pe_raster_resource(b);
			if (!r || x < 0 || y < 0 || x >= r->width ||
			    y >= r->height)
				return UINT32_MAX;
			uint32_t argb = 0xff000000u | ((color & 0xff) << 16) |
					(color & 0xff00u) |
					((color >> 16) & 0xff);
			if (!d->clip_active || pe_region_contains(&d->region, x, y))
				pe_raster_set(r, x, y, argb);
			return color;
		}
	}
	struct pg_packet p = {
		.op = PG_PIXEL, .a = x, .b = y, .color = pe_color_rgb(color)
	};
	return pe_draw(dc, &p) ? color : UINT32_MAX;
}
__attribute__((ms_abi)) BOOL MoveToEx(HANDLE dc, int x, int y, int32_t *old)
{
	PE_STATE_GUARD;
	struct pe_gui_object *d = pe_dc_lookup(dc);
	if (!d)
		return 0;
	if (old) {
		old[0] = d->u.dc.x;
		old[1] = d->u.dc.y;
	}
	d->u.dc.x = x;
	d->u.dc.y = y;
	return 1;
}
__attribute__((ms_abi)) BOOL LineTo(HANDLE dc, int x, int y)
{
	struct pg_packet p = { .op = PG_LINE, .c = x, .d = y };
	{
		PE_STATE_GUARD;
		struct pe_gui_object *d = pe_dc_lookup(dc);
		if (!d)
			return 0;
		struct pe_gui_object *pen =
			pe_gui_lookup(d->u.dc.pen, PE_GDI_PEN);
		if (!pen)
			return 0;
		if (pen->u.pen.style != 0 && pen->u.pen.style != 5) {
			SetLastError(50);
			return 0;
		}
		if (pen->u.pen.x > 1) {
			SetLastError(50);
			return 0;
		}
		p.a = d->u.dc.x;
		p.b = d->u.dc.y;
		p.color = pe_color_rgb(pen->u.pen.color);
		if (pen->u.pen.style == 5) {
			d->u.dc.x = x;
			d->u.dc.y = y;
			return 1;
		}
	}
	if (p.a < -32768 || p.b < -32768 || x < -32768 || y < -32768 ||
	    p.a > 32767 || p.b > 32767 || x > 32767 || y > 32767) {
		SetLastError(87);
		return 0;
	}
	if (!pe_draw(dc, &p))
		return 0;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *d = pe_dc_lookup(dc);
		if (d) {
			d->u.dc.x = x;
			d->u.dc.y = y;
		}
	}
	return 1;
}
__attribute__((ms_abi)) BOOL Rectangle(HANDLE dc, int left, int top, int right,
				       int bottom)
{
	HANDLE brush;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *d = pe_dc_lookup(dc);
		if (!d)
			return 0;
		brush = d->u.dc.brush;
	}
	if (right <= left || bottom <= top)
		return 1;
	struct pe_rect rect = { left, top, right, bottom };
	if (!FillRect(dc, &rect, brush))
		return 0;
	int32_t old[2];
	if (!MoveToEx(dc, left, top, old))
		return 0;
	BOOL ok = LineTo(dc, right - 1, top) &&
		  LineTo(dc, right - 1, bottom - 1) &&
		  LineTo(dc, left, bottom - 1) && LineTo(dc, left, top);
	MoveToEx(dc, old[0], old[1], NULL);
	return ok;
}
static void pe_gui_thread_cleanup(void)
{
	struct pe_ui *u = NULL;
	{
		PE_STATE_GUARD;
		for (struct pe_ui *v = pe_uis; v; v = v->next)
			if (v->tid == GetCurrentThreadId()) {
				u = v;
				break;
			}
	}
	if (!u)
		return;
	struct pe_window *first;
	{
		PE_STATE_GUARD;
		first = pe_windows;
	}
	for (struct pe_window *w = first; w; w = w->next)
		if (w->ui == u && !w->dead)
			DestroyWindow(w->handle);
	pe_ui_cancel(u);
	pe_ui_disconnect(u);
	PE_STATE_GUARD;
	while (u->head) {
		struct pe_ui_message *m = u->head;
		u->head = m->next;
		raw_syscall3(SYS_munmap, (long)m, sizeof(*m), 0);
	}
	u->tail = NULL;
	u->tid = 0;
}
