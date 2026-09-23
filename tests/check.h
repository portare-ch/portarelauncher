/* The whole test framework: a few macros and some fixture helpers.
 *
 * Each tests/test_*.c is its own program, linked against only the modules it
 * tests, and exits non-zero if anything failed. `make test` runs them all.
 */
#ifndef PL_CHECK_H
#define PL_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int check_count, check_failed;

#define CHECK(cond) do { \
	check_count++; \
	if (!(cond)) { \
		check_failed++; \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", \
		        __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_INT(got, want) do { \
	long long g_ = (long long)(got), w_ = (long long)(want); \
	check_count++; \
	if (g_ != w_) { \
		check_failed++; \
		fprintf(stderr, "%s:%d: %s is %lld, want %lld\n", \
		        __FILE__, __LINE__, #got, g_, w_); \
	} \
} while (0)

#define CHECK_STR(got, want) do { \
	const char *g_ = (got), *w_ = (want); \
	check_count++; \
	if (!g_ || strcmp(g_, w_) != 0) { \
		check_failed++; \
		fprintf(stderr, "%s:%d: %s is \"%s\", want \"%s\"\n", \
		        __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); \
	} \
} while (0)

static inline int check_report(const char *name)
{
	printf("%-10s %4d checks, %d failed\n", name, check_count, check_failed);
	return check_failed ? 1 : 0;
}

/* A scratch directory under /tmp rather than $TMPDIR: on macOS that is a
 * path long enough to overflow the launcher's own path fields, which would
 * be testing the fixture rather than the code. */
static inline char *fixture_dir(void)
{
	static char dir[32];
	strcpy(dir, "/tmp/pl-test-XXXXXX");
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		exit(2);
	}
	return dir;
}

static inline void fixture_write(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

static inline void fixture_mkdir(const char *path)
{
	if (mkdir(path, 0755) < 0) {
		perror(path);
		exit(2);
	}
}

static inline char *fixture_read(const char *path)
{
	static char buf[4096];
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static inline void fixture_cleanup(const char *dir)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", dir);
}

#endif
