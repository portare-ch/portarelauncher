#include "scope.h"

#include <stdio.h>
#include <string.h>

struct map {
	unsigned code;
	enum scope_ctl ctl;
};

/* The same everywhere: shoulders, sticks, the middle row, the D-pad when
 * it comes as buttons. */
static const struct map common[] = {
	{ SC_BTN_TL, CTL_L1 },        { SC_BTN_TR, CTL_R1 },
	{ SC_BTN_TL2, CTL_L2 },       { SC_BTN_TR2, CTL_R2 },
	{ SC_BTN_THUMBL, CTL_L3 },    { SC_BTN_THUMBR, CTL_R3 },
	{ SC_BTN_SOUTH, CTL_BOTTOM }, { SC_BTN_EAST, CTL_EAST },
	{ SC_BTN_SELECT, CTL_SELECT }, { SC_BTN_MODE, CTL_HOME },
	{ SC_BTN_START, CTL_START },
	{ SC_BTN_DPAD_UP, CTL_UP },   { SC_BTN_DPAD_DOWN, CTL_DOWN },
	{ SC_BTN_DPAD_LEFT, CTL_LEFT }, { SC_BTN_DPAD_RIGHT, CTL_RIGHT },
};

/* InputPlumber's virtual pad names the face buttons by position, and the
 * paddles are the fifth and sixth extra triggers (quirk 070-modifiers). */
static const struct map virt[] = {
	{ SC_BTN_NORTH, CTL_TOP },    { SC_BTN_WEST, CTL_WEST },
	{ SC_BTN_TRIGGER_HAPPY3, CTL_M1 }, { SC_BTN_TRIGGER_HAPPY4, CTL_M2 },
};

/* The MCU has the top and left face buttons the other way round, and the
 * paddles are gpio-keys, BTN_Z on the left and BTN_C on the right: what
 * nova_mcu.yaml tells InputPlumber, read the other way. */
static const struct map raw[] = {
	{ SC_BTN_WEST, CTL_TOP },     { SC_BTN_NORTH, CTL_WEST },
	{ SC_BTN_Z, CTL_M1 },         { SC_BTN_C, CTL_M2 },
};

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int ctl_of(const struct scope *s, unsigned code)
{
	const struct map *m = s->layer == SCOPE_RAW ? raw : virt;
	int n = s->layer == SCOPE_RAW ? N(raw) : N(virt);
	for (int i = 0; i < n; i++)
		if (m[i].code == code)
			return (int)m[i].ctl;
	for (int i = 0; i < N(common); i++)
		if (common[i].code == code)
			return (int)common[i].ctl;
	return -1;
}

static int axis_of(unsigned code)
{
	switch (code) {
	case SC_ABS_X:  return AX_LX;
	case SC_ABS_Y:  return AX_LY;
	case SC_ABS_RX: return AX_RX;
	case SC_ABS_RY: return AX_RY;
	case SC_ABS_Z:  return AX_LT;
	case SC_ABS_RZ: return AX_RT;
	default:        return -1;
	}
}

void scope_reset(struct scope *s, enum scope_layer layer)
{
	memset(s, 0, sizeof(*s));
	s->layer = layer;
	for (int a = 0; a < N_AX; a++) {
		s->min[a] = 0;
		s->max[a] = 255;
	}
	/* At rest until the device says otherwise: centred sticks, released
	 * triggers. */
	s->value[AX_LX] = s->value[AX_LY] = s->value[AX_RX] = s->value[AX_RY] = 128;
}

void scope_range(struct scope *s, unsigned abs_code, int min, int max, int value)
{
	int a = axis_of(abs_code);
	if (a < 0 || max <= min)
		return;
	s->min[a] = min;
	s->max[a] = max;
	s->value[a] = value;
}

static void remember(struct scope *s, unsigned type, unsigned code, int value)
{
	/* The same code again updates its row in place. A moving stick sends
	 * X and Y in turn, and two rows that swap two hundred times a second
	 * cannot be read. */
	for (int i = 0; i < s->nlast; i++)
		if (s->last[i].type == type && s->last[i].code == code) {
			struct scope_event e = s->last[i];
			e.value = value;
			memmove(&s->last[1], &s->last[0], (size_t)i * sizeof(s->last[0]));
			s->last[0] = e;
			return;
		}
	int keep = s->nlast < SCOPE_LAST ? s->nlast : SCOPE_LAST - 1;
	memmove(&s->last[1], &s->last[0], (size_t)keep * sizeof(s->last[0]));
	s->last[0] = (struct scope_event){ type, code, value };
	s->nlast = keep + 1;
}

int scope_feed(struct scope *s, unsigned type, unsigned code, int value,
               long long t_ms)
{
	if (type == SC_EV_SYN) {
		if (code != SC_SYN_REPORT)
			return 0;
		s->rate_t[s->rate_head] = t_ms;
		s->rate_head = (s->rate_head + 1) % SCOPE_RATE_N;
		if (s->rate_n < SCOPE_RATE_N)
			s->rate_n++;
		return 0;
	}

	if (type == SC_EV_KEY) {
		if (value == 2)                /* autorepeat: no new state */
			return 0;
		remember(s, type, code, value);
		int c = ctl_of(s, code);
		if (c >= 0)
			s->held[c] = value != 0;
		return 1;
	}

	if (type == SC_EV_ABS) {
		remember(s, type, code, value);
		if (code == SC_ABS_HAT0X) {
			s->held[CTL_LEFT] = value < 0;
			s->held[CTL_RIGHT] = value > 0;
		} else if (code == SC_ABS_HAT0Y) {
			s->held[CTL_UP] = value < 0;
			s->held[CTL_DOWN] = value > 0;
		} else {
			int a = axis_of(code);
			if (a >= 0)
				s->value[a] = value;
		}
		return 1;
	}
	return 0;
}

double scope_axis(const struct scope *s, enum scope_axis a)
{
	double lo = s->min[a], hi = s->max[a];
	double v = s->value[a];
	if (hi <= lo)
		return 0.0;
	if (a == AX_LT || a == AX_RT) {
		double f = (v - lo) / (hi - lo);
		return f < 0 ? 0 : f > 1 ? 1 : f;
	}
	/* Centred on the middle of the range: 0..255 has its centre at 127.5,
	 * so a pad resting at 128 reads +0.00 rather than a bias. */
	double mid = (lo + hi) / 2, half = (hi - lo) / 2;
	double f = (v - mid) / half;
	return f < -1 ? -1 : f > 1 ? 1 : f;
}

int scope_lit(const struct scope *s, enum scope_ctl c)
{
	if (s->held[c])
		return 1;
	if (c == CTL_L2)
		return s->value[AX_LT] > s->min[AX_LT];
	if (c == CTL_R2)
		return s->value[AX_RT] > s->min[AX_RT];
	return 0;
}

int scope_rate(const struct scope *s, long long now_ms)
{
	int n = 0;
	for (int i = 0; i < s->rate_n; i++) {
		int k = (s->rate_head - 1 - i + SCOPE_RATE_N) % SCOPE_RATE_N;
		if (s->rate_t[k] <= now_ms - 1000)
			break;
		if (s->rate_t[k] <= now_ms)
			n++;
	}
	return n;
}

struct name {
	unsigned code;
	const char *name;
};

static const struct name key_names[] = {
	{ SC_BTN_SOUTH, "BTN_SOUTH" },   { SC_BTN_EAST, "BTN_EAST" },
	{ SC_BTN_NORTH, "BTN_NORTH" },   { SC_BTN_WEST, "BTN_WEST" },
	{ SC_BTN_C, "BTN_C" },           { SC_BTN_Z, "BTN_Z" },
	{ SC_BTN_TL, "BTN_TL" },         { SC_BTN_TR, "BTN_TR" },
	{ SC_BTN_TL2, "BTN_TL2" },       { SC_BTN_TR2, "BTN_TR2" },
	{ SC_BTN_SELECT, "BTN_SELECT" }, { SC_BTN_START, "BTN_START" },
	{ SC_BTN_MODE, "BTN_MODE" },     { SC_BTN_BACK, "BTN_BACK" },
	{ SC_BTN_THUMBL, "BTN_THUMBL" }, { SC_BTN_THUMBR, "BTN_THUMBR" },
	{ SC_BTN_DPAD_UP, "BTN_DPAD_UP" },     { SC_BTN_DPAD_DOWN, "BTN_DPAD_DOWN" },
	{ SC_BTN_DPAD_LEFT, "BTN_DPAD_LEFT" }, { SC_BTN_DPAD_RIGHT, "BTN_DPAD_RIGHT" },
	/* The kernel's own name is BTN_TRIGGER_HAPPY3, eighteen characters
	 * of a fifty-three column screen. */
	{ SC_BTN_TRIGGER_HAPPY3, "BTN_HAPPY3" },
	{ SC_BTN_TRIGGER_HAPPY4, "BTN_HAPPY4" },
};

static const struct name abs_names[] = {
	{ SC_ABS_X, "ABS_X" },   { SC_ABS_Y, "ABS_Y" },   { SC_ABS_Z, "ABS_Z" },
	{ SC_ABS_RX, "ABS_RX" }, { SC_ABS_RY, "ABS_RY" }, { SC_ABS_RZ, "ABS_RZ" },
	{ SC_ABS_HAT0X, "ABS_HAT0X" }, { SC_ABS_HAT0Y, "ABS_HAT0Y" },
};

const char *scope_code_name(unsigned type, unsigned code, char *buf, size_t n)
{
	const struct name *t = type == SC_EV_ABS ? abs_names : key_names;
	int cnt = type == SC_EV_ABS ? N(abs_names) : N(key_names);
	for (int i = 0; i < cnt; i++)
		if (t[i].code == code)
			return t[i].name;
	snprintf(buf, n, "%s 0x%03x", type == SC_EV_ABS ? "ABS" : "KEY", code);
	return buf;
}
