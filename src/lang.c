#include "lang.h"

#include <string.h>

static enum lang current = LANG_EN;

enum lang lang_get(void) { return current; }
void lang_set(enum lang l) { current = l < N_LANG ? l : LANG_EN; }

enum lang lang_parse(const char *value)
{
	if (value && strncmp(value, "ja", 2) == 0 &&
	    (value[2] == '\0' || value[2] == '_'))
		return LANG_JA;
	return LANG_EN;
}

const char *lang_value(enum lang l) { return l == LANG_JA ? "ja_JP" : "en_US"; }
const char *lang_name(enum lang l)  { return l == LANG_JA ? "日本語" : "English"; }

/* Japanese per Nintendo, Sony and Sega's own usage in Japan; the hints keep
 * the key letter and translate the verb. 決定 where A enters or confirms,
 * 変更 where it changes a value in place. */
static const struct {
	const char *s[N_LANG];
	int max;
} table[N_STR] = {
	[S_VOL] = { { "VOL", "音量" }, 6 },
	[S_BRI] = { { "BRI", "明るさ" }, 6 },
	[S_BAT] = { { "BAT", "電池" }, 6 },
	[S_CHG] = { { "CHG", "充電" }, 6 },

	[S_QUICK]         = { { "QUICK ACCESS", "クイックアクセス" }, 24 },
	[S_RECENT]        = { { "Recently played", "最近遊んだゲーム" }, 24 },
	[S_FAVS]          = { { "Favourites", "お気に入り" }, 24 },
	[S_SYSTEMS]       = { { "SYSTEMS", "ゲーム機" }, 24 },
	[S_TOOLS]         = { { "Tools", "ツール" }, 24 },
	[S_NO_GAMES]      = { { "No games found.", "ゲームが見つかりません。" }, 47 },
	[S_ADD_GAMES]     = { { "Add games to roms/<system>.", "roms/<system> にゲームを入れてください。" }, 47 },
	[S_PRESS_REFRESH] = { { "Press %c to refresh.", "%c を押すと再読み込みします。" }, 47 },
	[S_HINT_SYSTEMS]  = { { "%c SELECT   %c SETTINGS", "%c 決定   %c 設定" }, 51 },

	[S_HINT_GAMES] = { { "%c LAUNCH   %c BACK   %c %s", "%c 起動   %c 戻る   %c %s" }, 30 },
	[S_FAVOURITE]  = { { "FAVOURITE", "お気に入り" }, 10 },
	[S_REMOVE]     = { { "REMOVE", "解除" }, 10 },
	[S_NO_PLAYED]  = { { "No games played yet.", "まだ遊んだゲームはありません。" }, 47 },
	[S_NO_FAVS]    = { { "No favourites yet.", "お気に入りはまだありません。" }, 47 },
	[S_ADD_FAV]    = { { "Press %c on a game to add a favourite.", "ゲームで %c を押すとお気に入りに追加します。" }, 47 },

	[S_SETTINGS]     = { { "Settings", "設定" }, 10 },
	[S_SET_WIFI]     = { { "Wi-Fi", "Wi-Fi" }, 24 },
	[S_SET_SSH]      = { { "SSH", "SSH" }, 24 },
	[S_SET_BT]       = { { "Bluetooth", "Bluetooth" }, 24 },
	[S_SET_USB]      = { { "USB mode", "USBモード" }, 24 },
	[S_SET_BUTTONS]  = { { "Button style", "ボタン表記" }, 24 },
	[S_SET_CONSOLES] = { { "Consoles", "ゲーム機" }, 24 },
	[S_SET_COLOR]    = { { "Color", "配色" }, 24 },
	[S_SET_PROFILE]  = { { "Color profile", "カラープロファイル" }, 24 },
	[S_SET_CHARGING] = { { "Charging LED", "充電LED" }, 24 },
	[S_SET_REGION]   = { { "Language & region", "言語と地域" }, 24 },
	[S_SET_DIAG]     = { { "Diagnostics", "診断" }, 24 },
	[S_SET_ABOUT]    = { { "About", "情報" }, 24 },
	[S_SET_POWER]    = { { "Power", "電源" }, 24 },

	[S_ON]                 = { { "on", "オン" }, 20 },
	[S_OFF]                = { { "off", "オフ" }, 20 },
	[S_CONNECTED]          = { { "connected", "接続中" }, 20 },
	[S_DISCONNECTED]       = { { "disconnected", "未接続" }, 20 },
	[S_NO_DEVICES]         = { { "no devices", "デバイスなし" }, 20 },
	[S_SHAPES]             = { { "Shapes", "記号" }, 20 },
	[S_DEFAULTS]           = { { "defaults", "標準" }, 20 },
	[S_N_CHANGED]          = { { "%d changed", "変更 %d件" }, 20 },
	[S_STOCK]              = { { "stock", "標準" }, 20 },
	[S_RESTART_TO_INSTALL] = { { "restart to install", "再起動でインストール" }, 20 },
	[S_UNKNOWN]            = { { "unknown", "不明" }, 20 },

	[S_DESC_CONSOLES] = { { "Experimental: reduce input lag per console.", "試験的: ゲーム機ごとに入力遅延を減らします。" }, 90 },
	[S_ADDRESS_FMT]   = { { "address  %s", "アドレス  %s" }, 12 },
	[S_DESC_PROFILE]  = { { "Experimental: may lose detail in dark areas.", "試験的: 暗い部分の階調が失われることがあります。" }, 90 },
	[S_NO_PROFILES]   = { { "No colour profiles installed.", "カラープロファイルがありません。" }, 45 },
	[S_DESC_CHARGING] = { { "Experimental: yellow thumbsticks when charging.", "試験的: 充電中はスティックが黄色に光ります。" }, 90 },
	[S_DESC_BT]       = { { "Scan and connect devices.", "デバイスを検索して接続します。" }, 45 },
	[S_DESC_REGION]   = { { "Language and time zone.", "言語とタイムゾーンを設定します。" }, 45 },
	[S_DESC_POWER]    = { { "Restart or power off.", "再起動または電源オフ。" }, 45 },
	[S_DESC_ABOUT]    = { { "Version, address, updates.", "バージョン、アドレス、アップデート。" }, 45 },
	[S_DESC_USB]      = { { "USB networking or file transfer.", "USBネットワークまたはファイル転送。" }, 45 },
	[S_HINT_SETTINGS] = { { "%c CHANGE   %c BACK", "%c 決定   %c 戻る" }, 51 },
	[S_HINT_CHANGE]   = { { "%c CHANGE   %c BACK", "%c 変更   %c 戻る" }, 51 },
	[S_HINT_OPEN]     = { { "%c OPEN   %c BACK", "%c 開く   %c 戻る" }, 51 },
	[S_HINT_SELECT]   = { { "%c SELECT   %c BACK", "%c 決定   %c 戻る" }, 51 },
	[S_FACE_CONFIRM]  = { { "confirm", "決定" }, 29 },
	[S_FACE_BACK]     = { { "back", "戻る" }, 29 },
	[S_FACE_SETTINGS] = { { "settings", "設定" }, 29 },

	[S_PRMPT_ON]     = { { "PRMPT on", "PRMPT オン" }, 20 },
	[S_PRMPT_OFF]    = { { "PRMPT off", "PRMPT オフ" }, 20 },
	[S_EXPERIMENTAL] = { { "Experimental.", "試験的な機能です。" }, 45 },
	[S_PRMPT_DESC]   = { { "PRMPT adds a pre-emptive frame to reduce input lag.", "PRMPTは先読みフレームを1つ加えて入力遅延を減らします。" }, 90 },

	[S_LANGUAGE_ROW] = { { "Language / 言語", "言語 / Language" }, 24 },
	[S_TIMEZONE]     = { { "Time zone", "タイムゾーン" }, 24 },

	[S_WIFI_TURN_ON]     = { { "Turn on Wi-Fi to find networks.", "Wi-Fiをオンにするとネットワークを探します。" }, 47 },
	[S_NETWORKS]         = { { "NETWORKS", "ネットワーク" }, 24 },
	[S_N_FOUND]          = { { "%d found", "%d件" }, 12 },
	[S_SAVED]            = { { "saved", "保存済み" }, 12 },
	[S_HINT_WIFI_SWITCH] = { { "%c SWITCH   %c BACK", "%c 切替   %c 戻る" }, 51 },
	[S_HINT_WIFI]        = { { "%c CONNECT   %c BACK   %c RESCAN", "%c 接続   %c 戻る   %c 再検索" }, 51 },

	[S_AUTOCONNECT]   = { { "Auto-connect paired devices", "ペアリング済み機器に自動接続" }, 30 },
	[S_YES]           = { { "yes", "はい" }, 12 },
	[S_NO]            = { { "no", "いいえ" }, 12 },
	[S_DEVICES]       = { { "DEVICES", "デバイス" }, 24 },
	[S_BT_NONE]       = { { "No devices found. Scan to find devices.", "デバイスがありません。検索してください。" }, 47 },
	[S_BT_OFF]        = { { "Bluetooth is off.", "Bluetoothはオフです。" }, 47 },
	[S_PAIRED]        = { { "paired", "ペアリング済み" }, 14 },
	[S_NOT_TRUSTED]   = { { "not trusted", "未承認" }, 14 },
	[S_NEW]           = { { "new", "新規" }, 14 },
	[S_HINT_BT]       = { { "%c %s   %c BACK   %c SCAN", "%c %s   %c 戻る   %c 検索" }, 40 },
	[S_BT_CHANGE]     = { { "CHANGE", "変更" }, 10 },
	[S_BT_DISCONNECT] = { { "DISCONNECT", "切断" }, 10 },
	[S_BT_CONNECT]    = { { "CONNECT", "接続" }, 10 },

	[S_TOOLS_HEAD] = { { "TOOLS", "ツール" }, 24 },
	[S_HINT_TOOLS] = { { "%c RUN   %c BACK", "%c 実行   %c 戻る" }, 51 },

	[S_NETWORK]       = { { "NETWORK", "ネットワーク" }, 12 },
	[S_PASSWORD]      = { { "PASSWORD", "パスワード" }, 12 },
	[S_PW_COUNT_MIN]  = { { "%d / %d  at least %d", "%d / %d  %d文字以上" }, 24 },
	[S_PW_SHOW]       = { { "SELECT: show or hide password", "SELECT: パスワードの表示・非表示" }, 47 },
	[S_HINT_KEYBOARD] = { { "%c TYPE  %c DELETE  %c SPACE  L1 SHIFT  START JOIN", "%c 入力  %c 削除  %c 空白  L1 シフト  START 接続" }, 51 },

	[S_SCREEN_OFF]      = { { "Screen off", "画面オフ" }, 24 },
	[S_SLEEP_AFTER]     = { { "Sleep after screen off", "画面オフからスリープまで" }, 24 },
	[S_RESTART]         = { { "Restart", "再起動" }, 24 },
	[S_POWER_OFF]       = { { "Power off", "電源オフ" }, 24 },
	[S_MIN]             = { { "%d min", "%d分" }, 12 },
	[S_DESC_BLANK]      = { { "Screen timeout while in menus. Press any button to wake. Does not affect games.", "メニュー表示中に画面を消すまでの時間です。ボタンを押すと戻ります。ゲーム中は消えません。" }, 135 },
	[S_DESC_SLEEP]      = { { "Time from the screen going off in menus to the device sleeping. Not while SSH is in use.", "メニューで画面が消えてから本体がスリープするまでの時間です。SSH使用中はスリープしません。" }, 135 },
	[S_ARMED_RESTART]   = { { "Press %c again to restart.", "もう一度 %c を押すと再起動します。" }, 47 },
	[S_ARMED_OFF]       = { { "Press %c again to switch off.", "もう一度 %c を押すと電源を切ります。" }, 47 },
	[S_FAIL_RESTART]    = { { "Could not restart (%d).", "再起動できませんでした (%d)。" }, 47 },
	[S_FAIL_OFF]        = { { "Could not switch off (%d).", "電源を切れませんでした (%d)。" }, 47 },

	[S_REGION_HEAD] = { { "REGION", "地域" }, 24 },
	[S_CITY]        = { { "CITY", "都市" }, 24 },
	[S_CURRENT]     = { { "current", "現在" }, 10 },
	[S_HINT_SET]    = { { "%c SET   %c BACK", "%c 設定   %c 戻る" }, 51 },
	[S_SAVING_TZ]   = { { "Saving time zone...", "タイムゾーンを保存中..." }, 47 },
	[S_TZ_FAIL]     = { { "Could not save time zone.", "タイムゾーンを保存できませんでした。" }, 47 },

	[S_UPDATE]       = { { "Update", "アップデート" }, 24 },
	[S_AUTOMATIC]    = { { "automatic", "自動" }, 20 },
	[S_VERSION]      = { { "version", "バージョン" }, 10 },
	[S_COMMIT]       = { { "commit", "コミット" }, 10 },
	[S_BUILT]        = { { "built", "ビルド" }, 10 },
	[S_DEVICE]       = { { "device", "デバイス" }, 10 },
	[S_ADDRESS]      = { { "address", "アドレス" }, 10 },
	[S_OFFLINE]      = { { "offline", "オフライン" }, 20 },
	[S_PASSWORD_L]   = { { "password", "パスワード" }, 10 },
	[S_LAUNCHER]     = { { "launcher", "ランチャー" }, 10 },
	[S_CHANNEL]      = { { "Channel", "チャンネル" }, 24 },
	[S_ACT_RESTART]  = { { "Restart to install", "再起動してインストール" }, 30 },
	[S_ACT_DOWNLOAD] = { { "Download and install", "ダウンロードしてインストール" }, 30 },
	[S_ACT_CHECK]    = { { "Check again", "再確認" }, 30 },
	[S_INSTALLED]    = { { "installed", "導入済み" }, 11 },
	[S_LATEST]       = { { "latest", "最新" }, 11 },
	[S_UPD_VERIFIED] = { { "Update verified. Restart to install.", "検証済みです。再起動するとインストールします。" }, 47 },
	[S_UPD_AVAILABLE] = { { "An update is available.", "アップデートがあります。" }, 47 },
	[S_UPD_CURRENT]  = { { "Up to date on this channel.", "このチャンネルの最新版です。" }, 47 },
	[S_UPD_NEWER]    = { { "Installed build is newer than this channel.", "導入済みのビルドの方が新しいです。" }, 47 },
	[S_UPD_NONE]     = { { "No builds for this device on this channel.", "このチャンネルにこの機種のビルドはありません。" }, 47 },
	[S_REL_1]        = { { "Release: monthly builds.", "Release: 月に一度のビルド。" }, 47 },
	[S_REL_2]        = { { "Fewer changes; longer testing.", "変更が少なく、長く試験されています。" }, 47 },
	[S_NIGHT_1]      = { { "Nightly: latest builds.", "Nightly: 最新のビルド。" }, 47 },
	[S_NIGHT_2]      = { { "Latest fixes; may be less stable.", "最新の修正入り。不安定なことがあります。" }, 47 },
	[S_CHECKING]     = { { "checking for updates...", "アップデートを確認中..." }, 47 },
	[S_VERIFYING]    = { { "Verifying download...", "ダウンロードを検証中..." }, 47 },
	[S_DOWNLOADING]  = { { "Downloading", "ダウンロード中" }, 47 },
	[S_MB_OF]        = { { "%d of %d MB", "%d / %d MB" }, 47 },
	[S_KEEP_NET]     = { { "Keep your network connected.", "ネットワークに接続したままにしてください。" }, 47 },
	[S_RETRY_RESUME] = { { "Retry an interrupted download to resume.", "中断したダウンロードは再試行で再開できます。" }, 47 },

	[S_STARTING]      = { { "Starting...", "起動中..." }, 45 },
	[S_CANNOT_LAUNCH] = { { "Cannot launch this game.", "このゲームは起動できません。" }, 47 },
	[S_NO_EMULATOR]   = { { "No emulator configured.", "エミュレーターが設定されていません。" }, 47 },
	[S_NO_CORE]       = { { "No core configured.", "コアが設定されていません。" }, 47 },

	[S_JOINING]       = { { "joining %s...", "%s に接続中..." }, 15 },
	[S_PW_REJECTED]   = { { "Password rejected. Check and retry.", "パスワードが違います。再入力してください。" }, 47 },
	[S_OUT_OF_RANGE]  = { { "Network out of range.", "ネットワークが圏外です。" }, 47 },
	[S_NO_RESPONSE]   = { { "No response. Move closer and retry.", "応答がありません。近づいて再試行してください。" }, 47 },
	[S_CONN_FAILED]   = { { "Connection failed (error %d).", "接続できませんでした (エラー %d)。" }, 47 },

	[S_SCANNING]        = { { "scanning...", "検索中..." }, 47 },
	[S_READING_DEVICES] = { { "reading devices...", "デバイスを読み込み中..." }, 47 },
	[S_SSH_STARTING]    = { { "starting ssh...", "SSHを開始中..." }, 47 },
	[S_SSH_STOPPING]    = { { "stopping ssh...", "SSHを停止中..." }, 47 },
	[S_SWITCHING]       = { { "switching...", "切替中..." }, 47 },
	[S_PROFILE_FAIL]    = { { "Could not apply colour profile.", "カラープロファイルを適用できませんでした。" }, 47 },
	[S_WIFI_ON_BUSY]    = { { "switching Wi-Fi on...", "Wi-Fiをオンにしています..." }, 47 },
	[S_WIFI_OFF_BUSY]   = { { "switching Wi-Fi off...", "Wi-Fiをオフにしています..." }, 47 },
	[S_WIFI_FAIL]       = { { "Could not enable Wi-Fi.", "Wi-Fiをオンにできませんでした。" }, 47 },
	[S_RESCANNING]      = { { "rescanning...", "再検索中..." }, 47 },
	[S_NO_ENTERPRISE]   = { { "Networks requiring a username are unsupported.", "ユーザー名が必要なネットワークは使えません。" }, 47 },
	[S_CONNECTING]      = { { "connecting...", "接続中..." }, 47 },
	[S_SCANNING_8]      = { { "scanning for 8 seconds...", "8秒間検索中..." }, 47 },
	[S_TURNING_OFF]     = { { "switching off...", "オフにしています..." }, 47 },
	[S_TURNING_ON]      = { { "switching on...", "オンにしています..." }, 47 },
	[S_APPLYING]        = { { "applying...", "適用中..." }, 47 },
	[S_DISCONNECTING]   = { { "disconnecting...", "切断中..." }, 47 },
};

const char *tr_in(enum lang l, enum str s)
{
	if (s < 0 || s >= N_STR || l >= N_LANG || !table[s].s[l])
		return "";
	return table[s].s[l];
}

const char *tr(enum str s) { return tr_in(current, s); }

int tr_max(enum str s)
{
	return s >= 0 && s < N_STR ? table[s].max : 0;
}

/* The Japanese names, by es_systems.cfg's name. Not listed: shown as the
 * full name, which is what a name like ScummVM or MAME is in Japan too. */
static const struct { const char *name, *ja; } systems[] = {
	{ "arcade",          "アーケード" },
	{ "atomiswave",      "アトミスウェイブ" },
	{ "dreamcast",       "ドリームキャスト" },
	{ "famicom",         "ファミリーコンピュータ" },
	{ "fds",             "ファミコン ディスクシステム" },
	{ "gamecube",        "ニンテンドー ゲームキューブ" },
	{ "gamegear",        "ゲームギア" },
	{ "gb",              "ゲームボーイ" },
	{ "gba",             "ゲームボーイアドバンス" },
	{ "gbah",            "ゲームボーイアドバンス (改造)" },
	{ "gbav",            "ゲームボーイアドバンス ビデオ" },
	{ "gbc",             "ゲームボーイカラー" },
	{ "gbch",            "ゲームボーイカラー (改造)" },
	{ "gbh",             "ゲームボーイ (改造)" },
	{ "genesis",         "メガドライブ" },
	{ "genh",            "メガドライブ (改造)" },
	{ "ggh",             "ゲームギア (改造)" },
	{ "imageviewer",     "スクリーンショット" },
	{ "mastersystem",    "マスターシステム" },
	{ "megacd",          "メガCD" },
	{ "megadrive",       "メガドライブ" },
	{ "megadrive-japan", "メガドライブ" },
	{ "megadriveh",      "メガドライブ (改造)" },
	{ "movies",          "動画" },
	{ "music",           "ミュージックプレイヤー" },
	{ "n64",             "NINTENDO64" },
	{ "n64dd",           "64DD" },
	{ "neocd",           "ネオジオCD" },
	{ "neogeo",          "ネオジオ" },
	{ "nes",             "ファミリーコンピュータ" },
	{ "ports",           "ポート" },
	{ "ps2",             "プレイステーション2" },
	{ "ps3",             "プレイステーション3" },
	{ "psp",             "プレイステーション・ポータブル" },
	{ "psx",             "プレイステーション" },
	{ "satellaview",     "サテラビュー" },
	{ "saturn",          "セガサターン" },
	{ "sega32x",         "スーパー32X" },
	{ "segacd",          "メガCD" },
	{ "sfc",             "スーパーファミコン" },
	{ "snes",            "スーパーファミコン" },
	{ "snesh",           "スーパーファミコン (改造)" },
	{ "snesmsu1",        "スーパーファミコン MSU-1" },
	{ "tools",           "ツール" },
	{ "wiiware",         "Wiiウェア" },
};

const char *lang_system(const char *name, const char *fullname)
{
	const char *full = fullname && *fullname ? fullname : name;
	if (current != LANG_JA)
		return full;
	for (size_t i = 0; i < sizeof(systems) / sizeof(systems[0]); i++)
		if (strcmp(systems[i].name, name) == 0)
			return systems[i].ja;
	return full;
}

static const char *word(const char *const pairs[][2], size_t n, const char *en)
{
	if (current != LANG_JA)
		return en;
	for (size_t i = 0; i < n; i++)
		if (strcmp(pairs[i][0], en) == 0)
			return pairs[i][1];
	return en;
}

const char *lang_palette(const char *name)
{
	static const char *const p[][2] = {
		{ "grey", "グレー" }, { "amber", "アンバー" }, { "orange", "オレンジ" },
		{ "green", "グリーン" }, { "ice", "アイス" },
	};
	return word(p, sizeof(p) / sizeof(p[0]), name);
}

const char *lang_usb_mode(const char *mode)
{
	static const char *const p[][2] = {
		{ "disabled", "無効" }, { "network", "ネットワーク" },
		{ "file_transfer", "ファイル転送" },
	};
	return word(p, sizeof(p) / sizeof(p[0]), mode);
}
