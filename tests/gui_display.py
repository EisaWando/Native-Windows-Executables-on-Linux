#!/usr/bin/env python3
"""Read the real X window pixels and inject native resize/key/close events.
Uses system Xlib via ctypes solely as a test driver; no Windows implementation.
"""
import ctypes as C
import subprocess
import time
from pathlib import Path

X = C.CDLL('libX11.so.6')
Display = C.c_void_p
Window = C.c_ulong

def bind(name, restype, *args):
    f = getattr(X, name)
    f.restype, f.argtypes = restype, list(args)
    return f

open_display = bind('XOpenDisplay', Display, C.c_char_p)
root_window = bind('XDefaultRootWindow', Window, Display)
query_tree = bind('XQueryTree', C.c_int, Display, Window, C.POINTER(Window), C.POINTER(Window), C.POINTER(C.POINTER(Window)), C.POINTER(C.c_uint))
fetch_name = bind('XFetchName', C.c_int, Display, Window, C.POINTER(C.c_void_p))
free = bind('XFree', C.c_int, C.c_void_p)
get_image = bind('XGetImage', C.c_void_p, Display, Window, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_ulong, C.c_int)
get_pixel = bind('XGetPixel', C.c_ulong, C.c_void_p, C.c_int, C.c_int)
destroy_image = bind('XDestroyImage', C.c_int, C.c_void_p)
resize = bind('XResizeWindow', C.c_int, Display, Window, C.c_uint, C.c_uint)
flush = bind('XSync', C.c_int, Display, C.c_int)
keysym_to_keycode = bind('XKeysymToKeycode', C.c_uint, Display, C.c_ulong)
send_event = bind('XSendEvent', C.c_int, Display, Window, C.c_int, C.c_long, C.c_void_p)
intern = bind('XInternAtom', C.c_ulong, Display, C.c_char_p, C.c_int)
close_display = bind('XCloseDisplay', C.c_int, Display)
# Other desktop clients can disappear while XQueryTree is being traversed.
# Trap X errors and turn failures in our own readback into Python assertions.
error_handler_type = C.CFUNCTYPE(C.c_int, Display, C.c_void_p)
@error_handler_type
def on_x_error(display, event):
    return 0
set_error_handler = bind('XSetErrorHandler', C.c_void_p, error_handler_type)
set_error_handler(on_x_error)
class ClassHint(C.Structure):
    _fields_ = [('name', C.c_void_p), ('class_name', C.c_void_p)]
get_class_hint = bind('XGetClassHint', C.c_int, Display, Window, C.POINTER(ClassHint))


class KeyEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int), ('display', Display),
                ('window', Window), ('root', Window), ('subwindow', Window), ('time', C.c_ulong),
                ('x', C.c_int), ('y', C.c_int), ('x_root', C.c_int), ('y_root', C.c_int),
                ('state', C.c_uint), ('keycode', C.c_uint), ('same_screen', C.c_int)]
class ClientEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int), ('display', Display),
                ('window', Window), ('message_type', C.c_ulong), ('format', C.c_int), ('data', C.c_long * 5)]
class Event(C.Union):
    _fields_ = [('key', KeyEvent), ('client', ClientEvent), ('pad', C.c_long * 24)]

def find_window(display):
    todo = [(root_window(display), 0)]
    while todo:
        window, depth = todo.pop()
        name = C.c_void_p()
        if fetch_name(display, window, C.byref(name)) and name.value:
            text = C.string_at(name)
            free(name)
            if text.startswith(b'binfmt_pe '):
                hint = ClassHint()
                if get_class_hint(display, window, C.byref(hint)):
                    matches = bool(hint.class_name) and C.string_at(hint.class_name) == b'BinfmtPE'
                    if hint.name:
                        free(hint.name)
                    if hint.class_name:
                        free(hint.class_name)
                    if matches:
                        return window
        if depth >= 3:
            continue
        root, parent, children, count = Window(), Window(), C.POINTER(Window)(), C.c_uint()
        if query_tree(display, window, C.byref(root), C.byref(parent), C.byref(children), C.byref(count)):
            todo.extend((children[i], depth+1) for i in range(count.value))
            if children:
                free(children)
    return None

def screenshot(display, window, width, height):
    image = get_image(display, window, 0, 0, width, height, C.c_ulong(-1).value, 2)
    assert image, 'XGetImage failed'
    try:
        return [get_pixel(image, x, y) & 0xffffff for y in range(height) for x in range(width)]
    finally:
        destroy_image(image)

def run_demo(display, close_method, artifact=False):
    p = subprocess.Popen(['tests/window_demo.exe'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic()+10
        while time.monotonic() < deadline:
            if p.poll() is not None:
                raise AssertionError(f'demo exited early: {p.communicate()}')
            window = find_window(display)
            if window:
                break
            time.sleep(.02)
        else:
            raise AssertionError('demo window not found on X server')
        time.sleep(.3)
        pixels = screenshot(display, window, 560, 240)
        assert pixels[5*560+5] == 0x182030, 'background pixel not presented'
        assert pixels[30*560+30] == 0x3791f0, 'rectangle pixel not presented'
        ink = sum(pixels[y*560+x] != 0x3791f0 for y in range(42,70) for x in range(40,360))
        assert ink > 100, f'text glyphs not presented: {ink}'
        if artifact:
            with Path('/tmp/pe-window-demo.ppm').open('wb') as out:
                out.write(b'P6\n560 240\n255\n')
                out.write(bytes(channel for rgb in pixels for channel in ((rgb>>16)&255,(rgb>>8)&255,rgb&255)))
        resize(display, window, 640, 300)
        flush(display, 0)
        time.sleep(.3)
        pixels = screenshot(display, window, 640, 300)
        assert pixels[280*640+620] == 0x182030, 'new area not repainted after resize'
        event = Event()
        if close_method == 'key':
            event.key = KeyEvent(2, 0, 1, display, window, root_window(display), 0, 0, 5, 5, 5, 5, 0, keysym_to_keycode(display, 0xff1b), 1)
            assert send_event(display, window, 0, 1, C.byref(event))
        else:
            event.client = ClientEvent(33, 0, 1, display, window, intern(display, b'WM_PROTOCOLS', 0), 32, (C.c_long*5)(intern(display, b'WM_DELETE_WINDOW', 0), 0, 0, 0, 0))
            assert send_event(display, window, 0, 0, C.byref(event))
        flush(display, 0)
        stdout, stderr = p.communicate(timeout=5)
        assert p.returncode == 0, (p.returncode, stdout, stderr)
        print(f'PASS X11 presentation, text pixels, resize repaint, {close_method} close')
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate()

display = open_display(None)
assert display, 'no X display; run on X11/XWayland outside the sandbox'
try:
    run_demo(display, 'key', True)
    run_demo(display, 'WM_DELETE_WINDOW')
finally:
    close_display(display)
