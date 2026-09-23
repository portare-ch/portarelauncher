#include "net.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NMCLI "/usr/bin/nmcli"
#define USBGADGET "/usr/bin/usbgadget"

/* Runs argv and feeds each output line to cb. No shell: every argument here
 * either came from a scan or from a profile name, and both are attacker
 * controlled in the sense that matters - somebody else names their network. */
static int run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx)
{
	int fd[2];
	if (pipe(fd) < 0)
		return -1;

	pid_t pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		int null = open("/dev/null", O_WRONLY);
		if (null >= 0) {
			dup2(null, STDERR_FILENO);
			close(null);
		}
		close(fd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fd[1]);
	FILE *f = fdopen(fd[0], "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\n")] = '\0';
			if (cb && line[0])
				cb(line, ctx);
		}
		fclose(f);
	} else {
		close(fd[0]);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* nmcli -t escapes ':' and '\' in values. Splits one terse line into fields,
 * unescaping as it goes. Returns how many it found. */
static int terse_split(const char *line, char out[][80], int max)
{
	int n = 0;
	size_t w = 0;
	if (max <= 0)
		return 0;
	out[0][0] = '\0';

	for (const char *p = line; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			if (w + 1 < 80)
				out[n][w++] = *p;
		} else if (*p == ':') {
			out[n][w] = '\0';
			if (++n >= max)
				return n;
			w = 0;
			out[n][0] = '\0';
		} else if (w + 1 < 80) {
			out[n][w++] = *p;
		}
	}
	out[n][w] = '\0';
	return n + 1;
}

static void copy_str(char *dst, size_t dsz, const char *src)
{
	size_t n = strlen(src);
	if (n >= dsz)
		n = dsz - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

/* ---- Wi-Fi ---------------------------------------------------------- */

static void cb_saved(char *line, void *ctx)
{
	struct net_list *l = ctx;
	char f[4][80];
	if (terse_split(line, f, 4) < 2)
		return;
	if (strcmp(f[1], "802-11-wireless") != 0)
		return;
	if (l->n >= NET_MAX || !f[0][0])
		return;

	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	copy_str(e->name, sizeof(e->name), f[0]);
	e->saved = 1;
	e->signal = -1;
}

static void cb_visible(char *line, void *ctx)
{
	struct net_list *l = ctx;
	char f[4][80];
	if (terse_split(line, f, 4) < 3 || !f[1][0])
		return;

	int active = f[0][0] == '*';
	int sig = atoi(f[2]);

	/* Already listed as a saved profile: fill in what the scan knows. */
	for (int i = 0; i < l->n; i++) {
		if (strcmp(l->e[i].name, f[1]) == 0) {
			if (sig > l->e[i].signal)
				l->e[i].signal = sig;
			if (active)
				l->e[i].active = 1;
			return;
		}
	}

	if (l->n >= NET_MAX)
		return;
	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	copy_str(e->name, sizeof(e->name), f[1]);
	e->signal = sig;
	e->active = active;
}

static int by_rank(const void *a, const void *b)
{
	const struct net_entry *x = a, *y = b;
	if (x->active != y->active) return y->active - x->active;
	if (x->saved  != y->saved)  return y->saved  - x->saved;
	return y->signal - x->signal;
}

void net_scan(struct net_list *l, int rescan)
{
	memset(l, 0, sizeof(*l));

	char *const saved[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                        (char *)"NAME,TYPE", (char *)"connection",
	                        (char *)"show", NULL };
	run(saved, cb_saved, l);

	if (rescan) {
		char *const rs[] = { (char *)NMCLI, (char *)"device", (char *)"wifi",
		                     (char *)"rescan", NULL };
		run(rs, NULL, NULL);
	}

	char *const vis[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                      (char *)"IN-USE,SSID,SIGNAL", (char *)"device",
	                      (char *)"wifi", (char *)"list", NULL };
	run(vis, cb_visible, l);

	qsort(l->e, (size_t)l->n, sizeof(l->e[0]), by_rank);
}

static void cb_first(char *line, void *ctx)
{
	char *out = ctx;
	if (!out[0])
		copy_str(out, 80, line);
}

int net_wifi_enabled(void)
{
	char v[80] = "";
	char *const argv[] = { (char *)NMCLI, (char *)"-t", (char *)"radio",
	                       (char *)"wifi", NULL };
	run(argv, cb_first, v);
	return strcmp(v, "enabled") == 0;
}

void net_wifi_set(int on)
{
	char *const argv[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                       (char *)(on ? "on" : "off"), NULL };
	run(argv, NULL, NULL);
}

int net_connect(const char *name)
{
	char *const argv[] = { (char *)NMCLI, (char *)"connection", (char *)"up",
	                       (char *)"id", (char *)name, NULL };
	return run(argv, NULL, NULL);
}

int net_disconnect(void)
{
	char *const argv[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                       (char *)"off", NULL };
	return run(argv, NULL, NULL);
}

static void cb_ip4(char *line, void *ctx)
{
	char *out = ctx;
	/* "IP4.ADDRESS[1]:192.168.1.42/24" */
	char *colon = strchr(line, ':');
	if (!colon || out[0])
		return;
	char *slash = strchr(colon + 1, '/');
	if (slash)
		*slash = '\0';
	copy_str(out, 40, colon + 1);
}

void net_address(char *out, size_t osz)
{
	char v[40] = "";
	char *const argv[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                       (char *)"IP4.ADDRESS", (char *)"device",
	                       (char *)"show", NULL };
	run(argv, cb_ip4, v);
	copy_str(out, osz, v);
}

/* ---- USB gadget ----------------------------------------------------- */

struct mode_ctx { char (*out)[24]; int *n; int max; };

static void cb_modes(char *line, void *ctx)
{
	struct mode_ctx *m = ctx;
	char *tok = strtok(line, " \t");
	while (tok && *m->n < m->max) {
		copy_str(m->out[(*m->n)++], 24, tok);
		tok = strtok(NULL, " \t");
	}
}

void usb_modes(char out[][24], int *n, int max)
{
	*n = 0;
	struct mode_ctx ctx = { out, n, max };
	char *const argv[] = { (char *)USBGADGET, (char *)"--options", NULL };
	run(argv, cb_modes, &ctx);
}

void usb_mode(char *out, size_t osz)
{
	char v[80] = "";
	char *const argv[] = { (char *)USBGADGET, NULL };
	run(argv, cb_first, v);
	copy_str(out, osz, v[0] ? v : "unknown");
}

int usb_set_mode(const char *mode)
{
	char *const argv[] = { (char *)USBGADGET, (char *)mode, NULL };
	return run(argv, NULL, NULL);
}

void usb_address(char *out, size_t osz)
{
	char v[80] = "";
	char *const argv[] = { (char *)USBGADGET, (char *)"address", NULL };
	run(argv, cb_first, v);
	copy_str(out, osz, v);
}
