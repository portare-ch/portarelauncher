#include "osinfo.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

/* KEY="value" or KEY=value, as os-release(5) allows. The build never writes
 * escapes inside the quotes, so only the quotes themselves are removed. */
static void take(const char *line, const char *key, char *out, size_t osz)
{
	size_t n = strlen(key);
	if (strncmp(line, key, n) != 0 || line[n] != '=')
		return;

	char v[128];
	str_copy(v, sizeof(v), line + n + 1);
	v[strcspn(v, "\r\n")] = '\0';

	char *p = v;
	size_t len = strlen(p);
	if (len >= 2 && (p[0] == '"' || p[0] == '\'') && p[len - 1] == p[0]) {
		p[len - 1] = '\0';
		p++;
	}
	str_copy(out, osz, p);
}

int osinfo_read(const char *path, struct osinfo *o)
{
	memset(o, 0, sizeof(*o));
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		take(line, "OS_VERSION",   o->version, sizeof(o->version));
		take(line, "OS_BUILD",     o->build,   sizeof(o->build));
		take(line, "BUILD_ID",     o->commit,  sizeof(o->commit));
		take(line, "BUILD_BRANCH", o->branch,  sizeof(o->branch));
		take(line, "BUILD_DATE",   o->date,    sizeof(o->date));
		take(line, "HW_DEVICE",    o->device,  sizeof(o->device));
		take(line, "HW_CPU",       o->cpu,     sizeof(o->cpu));
	}
	fclose(f);
	return 0;
}
