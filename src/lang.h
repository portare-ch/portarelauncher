/* The launcher in English or Japanese (docs/japanese.md).
 *
 * The language is system.language in system.cfg, en_US or ja_JP; anything
 * else reads as English. Every string the launcher owns is in one table,
 * both languages side by side, with the most columns it may take where it
 * is drawn; tests/test_lang.c holds every entry to that. Titles, network
 * and device names, time zone IDs, versions and addresses come from
 * outside and are drawn as they are.
 */
#ifndef PL_LANG_H
#define PL_LANG_H

enum lang { LANG_EN = 0, LANG_JA, N_LANG };

#define LANG_KEY "system.language"

enum lang lang_get(void);
void lang_set(enum lang l);
/* "ja_JP" or "ja" is Japanese; anything else, or nothing, English. */
enum lang lang_parse(const char *value);
/* What lang_set's language is stored as: "en_US", "ja_JP". */
const char *lang_value(enum lang l);
/* Each language named in itself: "English", "日本語". */
const char *lang_name(enum lang l);

enum str {
	/* the header */
	S_VOL, S_BRI, S_BAT, S_CHG,
	/* Systems */
	S_QUICK, S_RECENT, S_FAVS, S_SYSTEMS, S_TOOLS, S_NO_GAMES, S_ADD_GAMES,
	S_PRESS_REFRESH, S_HINT_SYSTEMS,
	/* lists of games */
	S_HINT_GAMES, S_FAVOURITE, S_REMOVE, S_NO_PLAYED, S_NO_FAVS, S_ADD_FAV,
	/* Settings */
	S_SETTINGS, S_SET_WIFI, S_SET_SSH, S_SET_BT, S_SET_USB, S_SET_BUTTONS,
	S_SET_CONSOLES, S_SET_COLOR, S_SET_PROFILE, S_SET_CHARGING, S_SET_REGION,
	S_SET_DIAG, S_SET_ABOUT, S_SET_POWER,
	S_ON, S_OFF, S_CONNECTED, S_DISCONNECTED, S_NO_DEVICES, S_SHAPES,
	S_DEFAULTS, S_N_CHANGED, S_STOCK, S_RESTART_TO_INSTALL, S_UNKNOWN,
	S_DESC_CONSOLES, S_ADDRESS_FMT, S_DESC_PROFILE, S_NO_PROFILES,
	S_DESC_CHARGING, S_DESC_BT, S_DESC_REGION, S_DESC_POWER, S_DESC_ABOUT,
	S_DESC_USB, S_HINT_SETTINGS, S_HINT_CHANGE, S_HINT_OPEN, S_HINT_SELECT,
	S_FACE_CONFIRM, S_FACE_BACK, S_FACE_SETTINGS,
	/* Settings > Consoles */
	S_PRMPT, S_INTEGER_SCALING, S_EXPERIMENTAL, S_PRMPT_DESC, S_INTEGER_DESC,
	/* Settings > Language & region */
	S_LANGUAGE_ROW, S_TIMEZONE,
	/* Wi-Fi */
	S_WIFI_TURN_ON, S_NETWORKS, S_N_FOUND, S_SAVED, S_HINT_WIFI_SWITCH,
	S_HINT_WIFI,
	/* Bluetooth */
	S_AUTOCONNECT, S_YES, S_NO, S_DEVICES, S_BT_NONE, S_BT_OFF, S_PAIRED,
	S_NOT_TRUSTED, S_NEW, S_HINT_BT, S_BT_CHANGE, S_BT_DISCONNECT, S_BT_CONNECT,
	/* Tools */
	S_TOOLS_HEAD, S_HINT_TOOLS,
	/* the keyboard */
	S_NETWORK, S_PASSWORD, S_PW_COUNT_MIN, S_PW_SHOW, S_HINT_KEYBOARD,
	/* Power */
	S_SCREEN_OFF, S_SLEEP_AFTER, S_RESTART, S_POWER_OFF, S_MIN, S_DESC_BLANK,
	S_DESC_SLEEP,
	S_ARMED_RESTART, S_ARMED_OFF, S_FAIL_RESTART, S_FAIL_OFF,
	/* Time zone */
	S_REGION_HEAD, S_CITY, S_CURRENT, S_HINT_SET, S_SAVING_TZ, S_TZ_FAIL,
	/* About and Update */
	S_UPDATE, S_AUTOMATIC, S_VERSION, S_COMMIT, S_BUILT, S_DEVICE, S_ADDRESS,
	S_OFFLINE, S_PASSWORD_L, S_LAUNCHER, S_CHANNEL, S_ACT_RESTART,
	S_ACT_DOWNLOAD, S_ACT_CHECK, S_INSTALLED, S_LATEST, S_UPD_VERIFIED,
	S_UPD_AVAILABLE, S_UPD_CURRENT, S_UPD_NEWER, S_UPD_NONE, S_REL_1, S_REL_2,
	S_NIGHT_1, S_NIGHT_2, S_CHECKING, S_VERIFYING, S_DOWNLOADING, S_MB_OF,
	S_KEEP_NET, S_RETRY_RESUME,
	/* launching */
	S_STARTING, S_CANNOT_LAUNCH, S_NO_EMULATOR, S_NO_CORE,
	/* joining a network */
	S_JOINING, S_PW_REJECTED, S_OUT_OF_RANGE, S_NO_RESPONSE, S_CONN_FAILED,
	/* while something runs */
	S_SCANNING, S_READING_DEVICES, S_SSH_STARTING, S_SSH_STOPPING, S_SWITCHING,
	S_PROFILE_FAIL, S_WIFI_ON_BUSY, S_WIFI_OFF_BUSY, S_WIFI_FAIL, S_RESCANNING,
	S_NO_ENTERPRISE, S_CONNECTING, S_SCANNING_8, S_TURNING_OFF, S_TURNING_ON,
	S_APPLYING, S_DISCONNECTING,
	N_STR
};

/* The string in the language in use. */
const char *tr(enum str s);
/* For the tests: in a given language, and the columns it may take. A
 * format is measured with %c as one column, %d as three, %s as none. */
const char *tr_in(enum lang l, enum str s);
int tr_max(enum str s);

/* A system as the language in use calls it: the name it was sold under in
 * Japan, or es_systems.cfg's full name. */
const char *lang_system(const char *name, const char *fullname);
/* A value that comes from elsewhere as an English word. */
const char *lang_palette(const char *name);
const char *lang_usb_mode(const char *mode);

#endif
