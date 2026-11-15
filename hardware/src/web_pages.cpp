#include "web_pages.h"

namespace web {
namespace {

WEB_TEXT(I18N_STRINGS)
const char *const WEB[][LANG_COUNT] PROGMEM = {WEB_TEXT(I18N_ROW)};
static_assert(sizeof(WEB) / sizeof(WEB[0]) == static_cast<size_t>(Txt::Count), "one row per Txt");

const char PAGE_HEAD_1[] PROGMEM = "<!doctype html><html lang=";
const char PAGE_HEAD_2[] PROGMEM =
    "><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>SmartClock</title>"
    "<style>body{font-family:sans-serif;max-width:560px;margin:1em auto;padding:0 1em;background:#111;color:#eee}"
    "input,select,button{font-size:1em;margin:.2em 0;padding:.4em;width:100%;box-sizing:border-box}"
    "input[type=checkbox]{width:auto}label{display:block;margin-top:.8em;color:#aaa}"
    "small{display:block;color:#888;margin:.1em 0}"
    "fieldset{border:1px solid #444;margin:1em 0}td{padding:.2em .6em .2em 0;vertical-align:top}"
    "a{color:#7cf}.warn{color:#fa4}</style></head><body>";
const char PAGE_TAIL[] PROGMEM = "</body></html>";

struct TlsModeOption {
  TlsMode mode;
  Txt label;
};
const TlsModeOption TLS_MODE_OPTIONS[] = {
    {TlsMode::Ca, Txt::TlsCa},
    {TlsMode::Fingerprint, Txt::TlsFingerprint},
    {TlsMode::Insecure, Txt::TlsInsecure},
};

const Txt LAYOUT_LABELS[LAYOUT_COUNT] = {Txt::LayoutA, Txt::LayoutB, Txt::LayoutC};

void head(Html &out) {
  out += FPSTR(PAGE_HEAD_1);
  out += settings::langName(out.lang());
  out += FPSTR(PAGE_HEAD_2);
}

void tail(Html &out) { out += FPSTR(PAGE_TAIL); }

void label(Html &out, Txt text, Txt help) {
  out += F("<label>");
  out += text;
  out += F("</label><small>");
  out += help;
  out += F("</small>");
}

void rowStart(Html &out, Txt key) {
  out += F("<tr><td>");
  out += key;
  out += F("</td><td>");
}

void rowEnd(Html &out) { out += F("</td></tr>"); }

void option(Html &out, const char *value, bool selected) {
  out += F("<option value=");
  out += value;
  if (selected) out += F(" selected");
  out += '>';
}

void csrfField(Html &out, const String &csrf) {
  out += F("<input type=hidden name=csrf value='");
  out += csrf;
  out += F("'>");
}

void usageStatus(Html &out, const Settings &s, const UsageState &u, uint32_t nowMs) {
  if (s.usageUrl.isEmpty()) {
    out += Txt::UrlNotSet;
  } else if (u.lastOk && (u.relayStatus == RelayStatus::Stale || u.relayStatus == RelayStatus::Error)) {
    out += F("<span class=warn>");
    out += u.relayStatus == RelayStatus::Stale ? Txt::RelayStale : Txt::RelayError;
    out.escaped(u.relayError);
    out += F("</span>");
  } else if (u.lastOk) {
    out += Txt::StatusOk;
    out += static_cast<unsigned long>((nowMs - u.lastSuccessMs) / 60000);
    out += Txt::MinutesAgo;
  } else if (u.lastAttemptMs) {
    out += F("<span class=warn>");
    out += Txt::StatusFailed;
    out.escaped(u.lastError);
    out += F("</span>");
  } else {
    out += Txt::Pending;
  }
}

void statusTable(Html &out, const Settings &s, const UsageState &u, const DeviceStatus &dev) {
  char clock[32];
  if (dev.now > usage::VALID_EPOCH) {
    struct tm t;
    localtime_r(&dev.now, &t);
    strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S", &t);
  } else {
    strncpy_P(clock, text(out.lang(), Txt::NotSynced), sizeof(clock) - 1);
    clock[sizeof(clock) - 1] = '\0';
  }
  out += F("<h2>SmartClock</h2><table>");
  rowStart(out, Txt::Firmware);
  out += F(FW_VERSION);
  rowEnd(out);
  out += F("<tr><td>Wi-Fi</td><td>");
  out.escaped(dev.ssid);
  out += F(" (");
  out += static_cast<long>(dev.rssi);
  out += F(" dBm)</td></tr><tr><td>IP</td><td>");
  out += dev.ip;
  rowEnd(out);
  rowStart(out, Txt::Time);
  out += clock;
  rowEnd(out);
  rowStart(out, Txt::FreeHeap);
  out += static_cast<unsigned long>(dev.freeHeap);
  rowEnd(out);
  rowStart(out, Txt::UsageFeed);
  usageStatus(out, s, u, dev.nowMs);
  rowEnd(out);
  rowStart(out, Txt::CertCheck);
  out += settings::tlsModeName(s.tlsMode);
  rowEnd(out);
  out += F("</table>");
}

// Language names stay in their own language so either can be found whatever the page language is.
void langField(Html &out, Lang selected) {
  out += F("<fieldset><legend>Language / 언어</legend><small>");
  out += Txt::LangHelp;
  out += F("</small><select name=lang>");
  option(out, settings::langName(Lang::En), selected == Lang::En);
  out += F("English</option>");
  option(out, settings::langName(Lang::Ko), selected == Lang::Ko);
  out += F("한국어</option></select></fieldset>");
}

void tokenWarning(Html &out, const Settings &s) {
  const TokenWarning w = settings::tokenWarning(s);
  if (w == TokenWarning::None) return;
  out += F("<p class=warn>");
  out += w == TokenWarning::NoCertCheck ? Txt::WarnNoCertCheck : Txt::WarnPlaintext;
  out += F("</p>");
}

void feedFields(Html &out, const Settings &s) {
  out += F("<fieldset><legend>");
  out += Txt::UsageFeed;
  out += F("</legend>");
  label(out, Txt::UrlLabel, Txt::UrlHelp);
  out += F("<input name=usage_url maxlength=200 value='");
  out.escaped(s.usageUrl);
  out += F("'>");
  label(out, Txt::TokenLabel, Txt::TokenHelp);
  out += F("<input name=usage_token type=password autocomplete=off maxlength=200 placeholder='");
  out += s.usageToken.length() ? Txt::TokenSet : Txt::TokenNotSet;
  out += F("'><label><input type=checkbox name=clear_token value=1> ");
  out += Txt::ClearToken;
  out += F("</label><small>");
  out += Txt::ClearTokenHelp;
  out += F("</small>");
  label(out, Txt::TlsLabel, Txt::TlsHelp);
  out += F("<select name=tls_mode>");
  for (const auto &m : TLS_MODE_OPTIONS) {
    option(out, settings::tlsModeName(m.mode), m.mode == s.tlsMode);
    out += m.label;
    out += F("</option>");
  }
  out += F("</select>");
  tokenWarning(out, s);
  label(out, Txt::FpLabel, Txt::FpHelp);
  out += F("<input name=tls_fp maxlength=64 value='");
  out.escaped(s.tlsFingerprint);
  out += F("'>");
  label(out, Txt::RefreshLabel, Txt::RefreshHelp);
  out += F("<input name=refresh_min type=number min=1 max=1440 value=");
  out += s.refreshMin;
  out += F("></fieldset>");
}

void displayFields(Html &out, const Settings &s) {
  out += F("<fieldset><legend>");
  out += Txt::DisplayLegend;
  out += F("</legend>");
  label(out, Txt::TzLabel, Txt::TzHelp);
  out += F("<input name=tz maxlength=48 value='");
  out.escaped(s.tz);
  out += F("'>");
  label(out, Txt::BrightLabel, Txt::BrightHelp);
  out += F("<input name=brightness type=number min=0 max=100 value=");
  out += s.brightness;
  out += '>';
  label(out, Txt::LayoutLabel, Txt::LayoutHelp);
  out += F("<select name=layout>");
  for (uint8_t i = 0; i < LAYOUT_COUNT; i++) {
    option(out, settings::layoutName(static_cast<Layout>(i)), static_cast<uint8_t>(s.layout) == i);
    out += LAYOUT_LABELS[i];
    out += F("</option>");
  }
  out += F("</select>");
  label(out, Txt::ModeLabel, Txt::ModeHelp);
  out += F("<select name=layout_mode>");
  option(out, settings::layoutModeName(LayoutMode::Fixed), s.layoutMode == LayoutMode::Fixed);
  out += Txt::ModeFixed;
  out += F("</option>");
  option(out, settings::layoutModeName(LayoutMode::Cycle), s.layoutMode == LayoutMode::Cycle);
  out += Txt::ModeCycle;
  out += F("</option></select>");
  label(out, Txt::CycleLabel, Txt::CycleHelp);
  out += F("<input name=cycle_sec type=number min=5 max=300 value=");
  out += s.cycleSec;
  out += F("></fieldset>");
}

void passwordFields(Html &out) {
  out += F("<fieldset><legend>");
  out += Txt::AdminLegend;
  out += F("</legend>");
  label(out, Txt::NewPassLabel, Txt::NewPassHelp);
  out += F("<input name=admin_pass type=password autocomplete=new-password maxlength=64><label>");
  out += Txt::RepeatLabel;
  out += F("</label><input name=admin_pass2 type=password autocomplete=new-password maxlength=64></fieldset>");
}

void maintenance(Html &out, const String &csrf) {
  struct Action {
    const char *path;
    Txt label;
  };
  const Action actions[] = {{"/reboot", Txt::Reboot}, {"/wifi-reset", Txt::WifiReset}, {"/factory-reset", Txt::FactoryReset}};
  out += F("<fieldset><legend>");
  out += Txt::Maintenance;
  out += F("</legend><p><a href=/update>");
  out += Txt::FirmwareUpdate;
  out += F("</a></p>");
  for (const auto &a : actions) {
    out += F("<form method=post action=");
    out += a.path;
    out += '>';
    csrfField(out, csrf);
    out += F("<button>");
    out += a.label;
    out += F("</button></form>");
  }
  out += F("</fieldset>");
}

void backLink(Html &out) {
  out += F("<p><a href=/>");
  out += Txt::Back;
  out += F("</a></p>");
}

}  // namespace

PGM_P text(Lang lang, Txt id) { return i18n::pick(WEB, static_cast<uint8_t>(id), lang); }

Html::Html(Lang lang, Sink sink) : lang_(lang), sink_(sink) { buf_.reserve(CHUNK + 128); }

void Html::spill() {
  if (buf_.length() >= CHUNK) flush();
}

void Html::flush() {
  if (buf_.length()) sink_(buf_);
  buf_ = "";
}

Html &Html::operator+=(const __FlashStringHelper *s) {
  buf_ += s;
  spill();
  return *this;
}

Html &Html::operator+=(const char *s) {
  buf_ += s;
  spill();
  return *this;
}

Html &Html::operator+=(const String &s) {
  buf_ += s;
  spill();
  return *this;
}

Html &Html::operator+=(char c) {
  buf_ += c;
  spill();
  return *this;
}

Html &Html::operator+=(int v) { return *this += static_cast<long>(v); }

Html &Html::operator+=(unsigned v) { return *this += static_cast<unsigned long>(v); }

Html &Html::operator+=(long v) {
  char num[12];
  snprintf(num, sizeof(num), "%ld", v);
  return *this += num;
}

Html &Html::operator+=(unsigned long v) {
  char num[12];
  snprintf(num, sizeof(num), "%lu", v);
  return *this += num;
}

Html &Html::operator+=(Txt id) { return *this += FPSTR(text(lang_, id)); }

void Html::escaped(const char *s) {
  for (; s && *s; s++) {
    switch (*s) {
      case '&': buf_ += F("&amp;"); break;
      case '<': buf_ += F("&lt;"); break;
      case '>': buf_ += F("&gt;"); break;
      case '"': buf_ += F("&quot;"); break;
      case '\'': buf_ += F("&#39;"); break;
      default: buf_ += *s;
    }
  }
  spill();
}

void adminPage(Html &out, const Settings &s, const UsageState &u, const DeviceStatus &dev, const String &csrf) {
  head(out);
  statusTable(out, s, u, dev);
  out += F("<form method=post action=/save>");
  csrfField(out, csrf);
  langField(out, s.lang);
  feedFields(out, s);
  displayFields(out, s);
  passwordFields(out);
  out += F("<button>");
  out += Txt::Save;
  out += F("</button></form>");
  maintenance(out, csrf);
  tail(out);
}

void portalPage(Html &out, const NetworkList &nets, PGM_P message) {
  head(out);
  out += F("<h2>");
  out += Txt::PortalTitle;
  out += F("</h2>");
  if (message) {
    out += F("<p class=warn>");
    out += FPSTR(message);
    out += F("</p>");
  }
  out += F("<form method=post action=/save>");
  langField(out, out.lang());
  label(out, Txt::Network, Txt::NetworkHelp);
  out += F("<select name=ssid_pick><option value=''>");
  out += Txt::TypeBelow;
  out += F("</option>");
  for (int i = 0; i < nets.count && i < 20; i++) {
    const String ssid = nets.ssid(i);
    out += F("<option value='");
    out.escaped(ssid);
    out += F("'>");
    out.escaped(ssid);
    out += F(" (");
    out += static_cast<long>(nets.rssi(i));
    out += F(" dBm)</option>");
  }
  out += F("</select>");
  label(out, Txt::OrSsid, Txt::OrSsidHelp);
  out += F("<input name=ssid maxlength=32>");
  label(out, Txt::WifiPass, Txt::WifiPassHelp);
  out += F("<input name=pass type=password autocomplete=off maxlength=64>");
  label(out, Txt::PortalAdmin, Txt::PortalAdminHelp);
  out += F("<input name=admin_pass type=password autocomplete=new-password maxlength=64><button>");
  out += Txt::SaveConnect;
  out += F("</button></form><p><a href=/rescan>");
  out += Txt::Rescan;
  out += F("</a></p>");
  tail(out);
}

void updatePage(Html &out, const String &csrf) {
  head(out);
  out += F("<h2>");
  out += Txt::FirmwareUpdate;
  out += F("</h2><p>");
  out += Txt::Current;
  out += F(FW_VERSION "</p><small>");
  out += Txt::UpdateHelp;
  out += F("</small><form method=post enctype=multipart/form-data action='/update?csrf=");
  out += csrf;
  out += F("'><input type=file name=firmware accept=.bin required><button>");
  out += Txt::UploadFlash;
  out += F("</button></form>");
  backLink(out);
  tail(out);
}

void messagePage(Html &out, Txt message, bool warn, bool back) {
  head(out);
  out += warn ? F("<p class=warn>") : F("<p>");
  out += message;
  out += F("</p>");
  if (back) backLink(out);
  tail(out);
}

void errorPage(Html &out, PGM_P prefix, const String &detail) {
  head(out);
  out += F("<p class=warn>");
  if (prefix) out += FPSTR(prefix);
  out.escaped(detail);
  out += F("</p>");
  backLink(out);
  tail(out);
}

}  // namespace web
