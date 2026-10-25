#ifndef PE_GUI_PROTOCOL_H
#define PE_GUI_PROTOCOL_H
#include <stdint.h>
#define PG_MAGIC 0x50454731u
#define PG_TEXT 1024
#define PG_MAX_DIM 4096
/* Local native-endian protocol, no pointers; response echoes opcode and id. */
enum {
	PG_HELLO = 1,
	PG_CREATE,
	PG_DESTROY,
	PG_SHOW,
	PG_TITLE,
	PG_FILL,
	PG_LINE,
	PG_TEXT_OUT,
	PG_PIXEL,
	PG_GET_PIXEL,
	PG_PRESENT,
	PG_POLL,
	PG_EVENT,
	PG_RESIZE,
	PG_VERIFY,
	PG_POSITION,
	PG_MENU_BEGIN,
	PG_MENU_ITEM,
	PG_MENU_END,
	PG_FONT_METRICS,
	PG_MEASURE
};
struct pg_text_metrics {
	int32_t height, ascent, descent, internal, external, average, maximum,
		weight, overhang, aspect_x, aspect_y;
	uint16_t first, last, fallback, space;
	uint8_t italic, underline, strike, pitch, charset;
};
_Static_assert(sizeof(struct pg_text_metrics) == 60, "TEXTMETRICW layout");
struct pg_packet {
	uint32_t magic, op;
	uint64_t id;
	int32_t status, a, b, c, d;
	uint32_t color, background, flags, length;
	char text[PG_TEXT];
};
_Static_assert(sizeof(struct pg_packet) == 1080, "GUI wire layout");
#endif
