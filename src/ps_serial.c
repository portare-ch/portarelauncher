#include "ps_serial.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

int ps_serial_valid(const char *s)
{
	if (strlen(s) != 10 || s[4] != '-') return 0;
	for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
	int neocd = !memcmp(s,"NGCD-0",6);
	for (int i = 5; i < 10; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (neocd && s[i] >= 'A' && s[i] <= 'F'))) return 0;
	return 1;
}

static int boot_name(const char *a, const char *b, char out[PS_SERIAL_SIZE])
{
	while (a < b && isspace((unsigned char)*a)) a++;
	while (b > a && isspace((unsigned char)b[-1])) b--;
	for (const char *p = a; p < b; p++)
		if (*p == '/' || *p == '\\' || *p == ':') a = p + 1;
	const char *v = memchr(a, ';', (size_t)(b - a));
	if (v) {
		if (v + 1 == b) return 0;
		for (const char *p = v + 1; p < b; p++) if (!isdigit((unsigned char)*p)) return 0;
		b = v;
	}
	size_t n = 0;
	for (const char *p = a; p < b; p++) {
		/* Only the standard numeric separator, between the third and fourth digits. */
		if (*p == '.' && p - a == 8 && b - a == 11) continue;
		if (n >= PS_SERIAL_SIZE - 1) return 0;
		out[n++] = *p == '_' ? '-' : (char)toupper((unsigned char)*p);
	}
	out[n] = 0;
	return ps_serial_valid(out);
}

static int boot_key(const char *config, size_t size, const char *key, char serial[PS_SERIAL_SIZE])
{
	int found = 0;
	size_t keylen = strlen(key);
	serial[0] = 0;
	if (memchr(config, 0, size)) return 0;
	for (size_t at = 0; at < size;) {
		size_t end = at;
		while (end < size && config[end] != '\r' && config[end] != '\n') end++;
		const char *a = config + at, *b = config + end;
		while (a < b && isspace((unsigned char)*a)) a++;
		if ((size_t)(b - a) >= keylen && !strncasecmp(a, key, keylen)) {
			a += keylen;
			if (a < b && !isspace((unsigned char)*a) && *a != '=') goto next;
			while (a < b && isspace((unsigned char)*a)) a++;
			char s[PS_SERIAL_SIZE];
			if (a == b || *a++ != '=' || !boot_name(a, b, s)) return -1;
			if (found && strcmp(s, serial)) return -1;
			memcpy(serial, s, sizeof(s));
			found = 1;
		}
next:
		at = end + 1;
	}
	return found;
}

int ps_boot_serial(const char *config, size_t size, char serial[PS_SERIAL_SIZE])
{
	if (memchr(config, 0, size)) return 0;
	/* A PS2 boot directive takes precedence. An invalid one cannot fall through. */
	int rc = boot_key(config,size,"BOOT2",serial);
	return rc ? rc > 0 : boot_key(config,size,"BOOT",serial) > 0;
}
