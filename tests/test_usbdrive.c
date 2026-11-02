/* Copyright (C) 2026-present PortareOS */
#include "check.h"
#include "usbdrive.h"

static void test_folder(void)
{
	char *root = fixture_dir();
	char p[192];

	snprintf(p, sizeof(p), "%s/debian-13.1.0-amd64-netinst.iso", root); fixture_write(p, "abc");
	snprintf(p, sizeof(p), "%s/cachyos-desktop.ISO", root);  fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/rescue.img", root);           fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/notes.txt", root);            fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/.partial.iso", root);         fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/.iso", root);                 fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/download.iso.part", root);    fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/folder.iso", root);           fixture_mkdir(p);

	struct usbdrive d;
	CHECK_INT(usbdrive_load_from(&d, root), 3);
	CHECK_STR(d.dir, root);
	if (d.n == 3) {
		/* By name, case aside; the extension is dropped from the name. */
		CHECK_STR(d.e[0].name, "cachyos-desktop");
		CHECK_STR(d.e[0].file, "cachyos-desktop.ISO");
		CHECK_STR(d.e[1].name, "debian-13.1.0-amd64-netinst");
		CHECK_INT((int)d.e[1].bytes, 3);
		CHECK_STR(d.e[2].file, "rescue.img");

		snprintf(p, sizeof(p), "%s/rescue.img", root);
		CHECK_INT(usbdrive_find(&d, p), 2);
		snprintf(p, sizeof(p), "%s/notes.txt", root);
		CHECK_INT(usbdrive_find(&d, p), -1);
		CHECK_INT(usbdrive_find(&d, "/elsewhere/rescue.img"), -1);
	}
	fixture_cleanup(root);
}

static void test_missing(void)
{
	struct usbdrive d;
	CHECK_INT(usbdrive_load_from(&d, "/nonexistent/usbdrive"), 0);
	CHECK_INT(usbdrive_find(&d, "/nonexistent/usbdrive/a.iso"), -1);
}

static void test_size(void)
{
	char s[16];
	usbdrive_size(3188850688LL, s, sizeof(s));
	CHECK_STR(s, "3.2 GB");
	usbdrive_size(1000000000LL, s, sizeof(s));
	CHECK_STR(s, "1.0 GB");
	usbdrive_size(999999999LL, s, sizeof(s));
	CHECK_STR(s, "1000 MB");
	usbdrive_size(700000000LL, s, sizeof(s));
	CHECK_STR(s, "700 MB");
	usbdrive_size(0, s, sizeof(s));
	CHECK_STR(s, "0 MB");
}

int main(void)
{
	test_folder();
	test_missing();
	test_size();
	return check_report("usbdrive");
}
