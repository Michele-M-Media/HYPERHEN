/* Host unit tests for shellui toolbox_i18n. */
#include "test_harness.h"

#include "toolbox_i18n.hpp"
#include <onion/notify_i18n.h>

#include <cstring>
#include <string>

extern "C" void onion_test_system_language_configure(int result, int value);

using namespace toolbox_i18n;

static int test_default_zh(void) {
  set_lang(Lang::ZhHans);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.pkg"), "内容安装与管理") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game"), "游戏辅助") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game.sub"),
                               "管理游戏金手指与下载金手指合集") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display"), "监控与显示") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display.sub"),
                               "游戏覆盖层、主菜单显示与游戏选项入口") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.system.sub"),
                               "风扇、外部存储与光盘许可证激活") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.preferences"), "操作偏好") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.open_after_load"),
                               "HyperHEN 加载后自动打开") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.home_menu"), "主菜单") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.level"), "日志输出等级") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.max_bytes"), "日志文件大小上限") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.info"), "信息（推荐）") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.trace"), "跟踪") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.installer.sub"),
                               "打开系统安装界面，用于安装 PKG 游戏或应用") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("fan.enable.sub"),
                               "关闭时使用系统默认风扇策略") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rest.delay"), "延迟工具箱激活（秒）") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("account.warning"),
                               "激活账号后，你可能会丢失现有存档（账号 ID 会改变）。"
                               "请确认你接受这个风险后再继续。") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.remote_play.pairing_cancelled"),
                  "远程游玩配对已中止。") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.crash.main"),
                  "HyperHEN 已崩溃……\n\n请将 /data/HyperHEN/"
                  "HyperHEN_crash.log 附加到 GitHub Issue") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("overlay.pos.top"), "顶部贴边") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("boot.thanks"),
                               "HyperHEN READY notification") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("overlay.align.center"), "居中") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "为 %s 启用/禁用 %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.group.unnamed"), "来源") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.cheats.conflict"),
                  "%s 与 %s 冲突（%s）于 0x%s") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.made_by"),
                  "%s · Coded by MICHELE MEDIA") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.thanks"),
                  "HyperHEN READY — UNLEASH THE POWER!") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.conflict"),
                  "HyperHEN 已拒绝启动：%s 正在运行") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.game_menu"),
                               "★ HyperHEN 金手指") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.repo.download"),
                               "下载金手指合集") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.repo.download.desc"),
                               "鸣谢 TeeKay87（官方金手指仓库）& PS5_Xiexiyu（中文镜像仓库）") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.repo.mirror.auto"), "自动") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.options"),
                               "PKG 安装器选项") == 0);
  return 0;
}

static int test_en(void) {
  set_lang(Lang::En);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.pkg"), "Content Install & Management") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game"), "Game Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game.sub"),
                               "Manage cheats and download the cheat "
                               "collection") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display.sub"),
                               "In-game overlay, home menu display, and game "
                               "options entry") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.system.sub"),
                               "Fan, external storage, and disc license "
                               "activation") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.preferences"), "Preferences") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.open_after_load"),
                               "Automatically open after HyperHEN loads") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.home_menu"), "Home Menu") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.level"), "Log output level") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.max_bytes"), "Log file size limit") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("boot.thanks"),
                               "HyperHEN READY notification") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.info"),
                               "Information (recommended)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.trace"), "Trace") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("sc.off"), "Off (no shortcut)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("account.link"), "Account activation") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("account.warning"),
                               "You may lose your existing game saves because "
                               "the account ID will change. Continue only if "
                               "you accept that risk.") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.remote_play.pairing_cancelled"),
                  "Remote Play pairing cancelled.") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.crash.main"),
                  "HyperHEN has crashed ...\n\nPlease attach /data/HyperHEN/"
                  "HyperHEN_crash.log to a GitHub issue") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Enable/disable %s for %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.group.unnamed"), "Source") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.cheats.conflict"),
                  "%s conflicts with %s (%s) at 0x%s") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.made_by"),
                  "%s · Coded by MICHELE MEDIA") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.thanks"),
                  "HyperHEN READY — UNLEASH THE POWER!") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.boot.conflict"),
                  "HyperHEN refused to start: %s is already running") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("payload.start_stop_fmt"),
                               "Start/stop %s (path: %s) (%s)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("debug.np_env.sub"),
                               "Change the PlayStation Network environment "
                               "string; the console reboots after saving") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("linux.loader"), "HyperLinux Boot") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.linux.armed"),
                  "HyperLinux armed. Fully reboot the PS5, then launch "
                  "HyperHEN once.") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rest.delay"),
                               "Delay toolbox activation (seconds)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.game_menu"),
                               "★ HyperHEN Cheats") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.repo.download"),
                               "Download cheat collection") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.repo.download.desc"),
                               "Thanks to TeeKay87 (Official Cheats Repo) & "
                               "PS5_Xiexiyu (Chinese Mirror Repo).") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.installing"),
                               "HyperHEN is installing the selected PKG") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.select_all"), "Select all") == 0);
  return 0;
}

static int test_ar(void) {
  set_lang(Lang::Ar);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.ar"), "العربية") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "تفعيل/تعطيل %s لـ %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.game_menu"),
                               "★ غش HyperHEN") == 0);
  return 0;
}

static int test_zh_hant(void) {
  set_lang(Lang::ZhHant);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "金手指") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.home_menu"), "主畫面") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "為 %s 啟用/停用 %s") == 0);
  return 0;
}

static int test_ja(void) {
  set_lang(Lang::Ja);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") ==
                   0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "チート") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.ja"), "日本語") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "%s の %s を有効/無効") == 0);
  return 0;
}

static int test_fr(void) {
  set_lang(Lang::Fr);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") ==
                   0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Codes de triche") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.fr"), "Français") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Pour %s, activer/désactiver %s") == 0);
  return 0;
}

static int test_de(void) {
  set_lang(Lang::De);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Cheats") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.de"), "Deutsch") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Für %s: %s ein-/ausschalten") == 0);
  return 0;
}

static int test_ko(void) {
  set_lang(Lang::Ko);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "치트") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.ko"), "한국어") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "%s의 %s 사용/해제") == 0);
  return 0;
}

static int test_es(void) {
  set_lang(Lang::Es);
  TEST_ASSERT_TRUE(
      std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Trucos") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.es"), "Español") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Para %s, activar/desactivar %s") == 0);
  return 0;
}

static int test_pt_br(void) {
  set_lang(Lang::PtBr);
  TEST_ASSERT_TRUE(
      std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Cheats") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.pt_br"), "Português (Brasil)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Para %s, ativar/desativar %s") == 0);
  return 0;
}

static int test_it(void) {
  set_lang(Lang::It);
  TEST_ASSERT_TRUE(
      std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Trucchi") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.it"), "Italiano") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Per %s, attiva/disattiva %s") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(tr("linux.loader"), "Avvia HyperLinux") == 0);
  TEST_ASSERT_TRUE(
      std::strcmp(onion_notify_tr("notify.linux.unsupported"),
                  "HyperLinux non dispone di un profilo esatto per questo "
                  "firmware.") == 0);
  return 0;
}

static int test_ru(void) {
  set_lang(Lang::Ru);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Читы") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.ru"), "Русский") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Для %s: вкл./выкл. %s") == 0);
  return 0;
}

static int test_pl(void) {
  set_lang(Lang::Pl);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "Cheaty") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.pl"), "Polski") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Dla %s: włącz/wyłącz %s") == 0);
  return 0;
}

static int test_th(void) {
  set_lang(Lang::Th);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.link"), "สูตรโกง") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.th"), "ไทย") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "เปิด/ปิด %s สำหรับ %s") == 0);
  return 0;
}

static int test_apply_ui_lang(void) {
  apply_ui_lang(2);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);
  TEST_ASSERT_EQ_INT(2, active_ui_lang_value());
  apply_ui_lang(1);
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);
  TEST_ASSERT_EQ_INT(1, active_ui_lang_value());
  apply_ui_lang(3);
  TEST_ASSERT_TRUE(active_lang() == Lang::Ar);
  TEST_ASSERT_EQ_INT(3, active_ui_lang_value());
  apply_ui_lang(4);
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHant);
  TEST_ASSERT_EQ_INT(4, active_ui_lang_value());
  apply_ui_lang(5);
  TEST_ASSERT_TRUE(active_lang() == Lang::Ja);
  TEST_ASSERT_EQ_INT(5, active_ui_lang_value());
  apply_ui_lang(6);
  TEST_ASSERT_TRUE(active_lang() == Lang::Fr);
  TEST_ASSERT_EQ_INT(6, active_ui_lang_value());
  apply_ui_lang(7);
  TEST_ASSERT_TRUE(active_lang() == Lang::De);
  TEST_ASSERT_EQ_INT(7, active_ui_lang_value());
  apply_ui_lang(8);
  TEST_ASSERT_TRUE(active_lang() == Lang::Ko);
  TEST_ASSERT_EQ_INT(8, active_ui_lang_value());
  apply_ui_lang(9);
  TEST_ASSERT_TRUE(active_lang() == Lang::Es);
  TEST_ASSERT_EQ_INT(9, active_ui_lang_value());
  apply_ui_lang(10);
  TEST_ASSERT_TRUE(active_lang() == Lang::PtBr);
  TEST_ASSERT_EQ_INT(10, active_ui_lang_value());
  apply_ui_lang(11);
  TEST_ASSERT_TRUE(active_lang() == Lang::It);
  TEST_ASSERT_EQ_INT(11, active_ui_lang_value());
  apply_ui_lang(12);
  TEST_ASSERT_TRUE(active_lang() == Lang::Ru);
  TEST_ASSERT_EQ_INT(12, active_ui_lang_value());
  apply_ui_lang(13);
  TEST_ASSERT_TRUE(active_lang() == Lang::Pl);
  TEST_ASSERT_EQ_INT(13, active_ui_lang_value());
  apply_ui_lang(14);
  TEST_ASSERT_TRUE(active_lang() == Lang::Th);
  TEST_ASSERT_EQ_INT(14, active_ui_lang_value());
  apply_ui_lang(15);
  TEST_ASSERT_TRUE(active_lang() == Lang::Nl);
  TEST_ASSERT_EQ_INT(15, active_ui_lang_value());
  apply_ui_lang(20);
  TEST_ASSERT_TRUE(active_lang() == Lang::Tr);
  TEST_ASSERT_EQ_INT(20, active_ui_lang_value());
  apply_ui_lang(25);
  TEST_ASSERT_TRUE(active_lang() == Lang::Vi);
  TEST_ASSERT_EQ_INT(25, active_ui_lang_value());
  apply_ui_lang(27);
  TEST_ASSERT_TRUE(active_lang() == Lang::Uk);
  TEST_ASSERT_EQ_INT(27, active_ui_lang_value());
  apply_ui_lang(28);
  TEST_ASSERT_TRUE(active_lang() == Lang::PtPt);
  TEST_ASSERT_EQ_INT(28, active_ui_lang_value());
  apply_ui_lang(99); /* invalid → zh */
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);
  return 0;
}

static int test_system_lang_follows_query(void) {
  onion_test_system_language_configure(0, 11);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);

  onion_test_system_language_configure(0, 1);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);

  onion_test_system_language_configure(0, 6);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::Nl);

  onion_test_system_language_configure(0, 7);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::PtPt);

  onion_test_system_language_configure(0, 30);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::Uk);

  apply_system_or_ui_lang(2);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);
  onion_test_system_language_configure(0, 11);
  apply_system_or_ui_lang(2);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);

  onion_test_system_language_configure(0, 1);
  return 0;
}

static int test_system_lang_query_failure_keeps_current(void) {
  set_lang(Lang::ZhHans);
  onion_test_system_language_configure(-1, 1);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);

  set_lang(Lang::En);
  onion_test_system_language_configure(-1, 11);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);

  onion_test_system_language_configure(0, 1);
  return 0;
}

static int test_missing_key(void) {
  set_lang(Lang::En);
  TEST_ASSERT_STREQ("no.such.key", tr("no.such.key"));
  return 0;
}

static int test_format(void) {
  set_lang(Lang::ZhHans);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "为 Game 启用/禁用 God");
  set_lang(Lang::En);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Enable/disable Game for God");
  TEST_ASSERT_TRUE(format("about.build", "v1") == "Build: v1");
  set_lang(Lang::Ar);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "تفعيل/تعطيل Game لـ God");
  set_lang(Lang::ZhHant);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "為 Game 啟用/停用 God");
  set_lang(Lang::Ja);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Game の God を有効/無効");
  set_lang(Lang::Fr);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Pour Game, activer/désactiver God");
  set_lang(Lang::De);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Für Game: God ein-/ausschalten");
  set_lang(Lang::Ko);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Game의 God 사용/해제");
  set_lang(Lang::Es);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Para Game, activar/desactivar God");
  set_lang(Lang::PtBr);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Para Game, ativar/desativar God");
  set_lang(Lang::It);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Per Game, attiva/disattiva God");
  set_lang(Lang::Ru);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Для Game: вкл./выкл. God");
  set_lang(Lang::Pl);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Dla Game: włącz/wyłącz God");
  set_lang(Lang::Th);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "เปิด/ปิด Game สำหรับ God");
  set_lang(Lang::Nl);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Game voor God in-/uitschakelen");
  set_lang(Lang::Tr);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Game öğesini God için aç/kapat");
  set_lang(Lang::Vi);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Bật/tắt Game cho God");
  set_lang(Lang::Uk);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Увімкнути/вимкнути Game для God");
  set_lang(Lang::PtPt);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Para Game, ativar/desativar God");
  return 0;
}

static int test_nl(void) {
  set_lang(Lang::Nl);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.nl"), "Nederlands") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "%s voor %s in-/uitschakelen") == 0);
  return 0;
}

static int test_tr(void) {
  set_lang(Lang::Tr);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.tr"), "Türkçe") == 0);
  return 0;
}

static int test_vi(void) {
  set_lang(Lang::Vi);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.vi"), "Tiếng Việt") == 0);
  return 0;
}

static int test_uk(void) {
  set_lang(Lang::Uk);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.uk"), "Українська") == 0);
  return 0;
}

static int test_pt_pt(void) {
  set_lang(Lang::PtPt);
  TEST_ASSERT_TRUE(
      std::strcmp(tr("root.title"), "★HyperHEN Tools") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("lang.pt"), "Português (Portugal)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("account.user_fmt"), "Utilizador: %s") == 0);
  return 0;
}

extern "C" int test_toolbox_i18n_suite(void) {
  int fails = 0;
  fails += onion_test_run("i18n.default_zh", test_default_zh);
  fails += onion_test_run("i18n.en", test_en);
  fails += onion_test_run("i18n.ar", test_ar);
  fails += onion_test_run("i18n.zh_hant", test_zh_hant);
  fails += onion_test_run("i18n.ja", test_ja);
  fails += onion_test_run("i18n.fr", test_fr);
  fails += onion_test_run("i18n.de", test_de);
  fails += onion_test_run("i18n.ko", test_ko);
  fails += onion_test_run("i18n.es", test_es);
  fails += onion_test_run("i18n.pt_br", test_pt_br);
  fails += onion_test_run("i18n.it", test_it);
  fails += onion_test_run("i18n.ru", test_ru);
  fails += onion_test_run("i18n.pl", test_pl);
  fails += onion_test_run("i18n.th", test_th);
  fails += onion_test_run("i18n.nl", test_nl);
  fails += onion_test_run("i18n.tr", test_tr);
  fails += onion_test_run("i18n.vi", test_vi);
  fails += onion_test_run("i18n.uk", test_uk);
  fails += onion_test_run("i18n.pt_pt", test_pt_pt);
  fails += onion_test_run("i18n.apply_ui_lang", test_apply_ui_lang);
  fails += onion_test_run("i18n.system_lang_follows_query",
                          test_system_lang_follows_query);
  fails += onion_test_run("i18n.system_lang_query_failure_keeps_current",
                          test_system_lang_query_failure_keeps_current);
  fails += onion_test_run("i18n.missing_key", test_missing_key);
  fails += onion_test_run("i18n.format", test_format);
  return fails;
}
