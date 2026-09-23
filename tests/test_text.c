#include "check.h"
#include "text.h"

static void test_str_copy(void)
{
	char buf[6];

	str_copy(buf, sizeof(buf), "abc");
	CHECK_STR(buf, "abc");

	str_copy(buf, sizeof(buf), "abcde");            /* exactly fits */
	CHECK_STR(buf, "abcde");

	str_copy(buf, sizeof(buf), "abcdefgh");         /* clipped, terminated */
	CHECK_STR(buf, "abcde");

	memcpy(buf, "xxxxx", 6);
	str_copy(buf, 0, "abc");                        /* no room: untouched */
	CHECK_STR(buf, "xxxxx");
}

static void test_strip_ansi(void)
{
	char s[128];

	strcpy(s, "\033[1;39mHeadphones\033[0m");
	strip_ansi(s);
	CHECK_STR(s, "Headphones");

	/* bluetoothctl's prompt, colour and all. */
	strcpy(s, "[\033[0;94mbluetooth\033[0m]# Device");
	strip_ansi(s);
	CHECK_STR(s, "[bluetooth]# Device");

	strcpy(s, "plain text");
	strip_ansi(s);
	CHECK_STR(s, "plain text");

	/* A two-byte sequence drops both bytes. */
	strcpy(s, "a\033Mb");
	strip_ansi(s);
	CHECK_STR(s, "ab");

	/* Cut off mid-sequence: nothing past the escape survives, and
	 * nothing reads past the end. */
	strcpy(s, "name\033[1;3");
	strip_ansi(s);
	CHECK_STR(s, "name");

	strcpy(s, "name\033");
	strip_ansi(s);
	CHECK_STR(s, "name");
}

int main(void)
{
	test_str_copy();
	test_strip_ansi();
	return check_report("text");
}
