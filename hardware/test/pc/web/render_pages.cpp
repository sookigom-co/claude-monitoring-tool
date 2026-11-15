// Usage: render_pages <out-dir>; writes every web page in both languages as HTML, non-zero on failure.
#include <cstdlib>
#include <string>

#include "web_pages.h"

using web::Html;
using web::Txt;

namespace {

int failures = 0;
std::string page;
size_t chunks = 0;
size_t largestChunk = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    failures++;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

void collect(const String &chunk) {
  check(!chunk.empty(), "an empty chunk would end the chunked response early");
  page += chunk;
  chunks++;
  if (chunk.size() > largestChunk) largestChunk = chunk.size();
}

template <typename Build>
void render(const std::string &dir, const char *name, Lang lang, Build build) {
  page.clear();
  chunks = largestChunk = 0;
  Html out(lang, collect);
  build(out);
  out.flush();
  const std::string file = std::string(name) + "-" + settings::langName(lang) + ".html";
  std::printf("  %-22s %6zu bytes in %2zu chunks, largest %4zu\n", file.c_str(), page.size(), chunks, largestChunk);
  check(page.rfind(std::string("<!doctype html><html lang=") + settings::langName(lang) + ">", 0) == 0, file + " html lang");
  check(page.size() >= 14 && page.compare(page.size() - 14, 14, "</body></html>") == 0, file + " ends the page");
  check(largestChunk <= Html::CHUNK + 700, file + " chunk size bound");
  FILE *f = std::fopen((dir + "/" + file).c_str(), "wb");
  check(f != nullptr, "open " + file);
  if (!f) return;
  std::fwrite(page.data(), 1, page.size(), f);
  std::fclose(f);
}

const char *const SSIDS[] = {"home-2.4G", "cafe <guest>", "o'brien"};
String ssidAt(int i) { return SSIDS[i]; }
int32_t rssiAt(int i) { return -48 - 9 * i; }

void textTable() {
  std::puts("web text table: every entry present in both languages");
  for (uint8_t i = 0; i < static_cast<uint8_t>(Txt::Count); i++) {
    check(std::strlen(web::text(Lang::En, static_cast<Txt>(i))) > 0, "English entry " + std::to_string(i));
    check(std::strlen(web::text(Lang::Ko, static_cast<Txt>(i))) > 0, "Korean entry " + std::to_string(i));
  }
}

void pages(const std::string &dir) {
  std::puts("pages: admin, portal, update and messages in both languages");
  Settings s;
  s.usageUrl = "http://10.0.0.5:8787/clock/usage";
  s.usageToken = "secret";
  s.layoutMode = LayoutMode::Cycle;
  UsageState u;
  u.lastOk = true;
  u.haveData = true;
  u.lastSuccessMs = 1000;
  const web::DeviceStatus dev{"home-2.4G", -52, "192.168.0.31", 1794720420, 31544, 1000 + 7 * 60000};
  const web::NetworkList nets{3, ssidAt, rssiAt};
  for (Lang lang : {Lang::En, Lang::Ko}) {
    s.lang = lang;
    render(dir, "admin", lang, [&](Html &out) { web::adminPage(out, s, u, dev, "CSRFTOKEN"); });
    render(dir, "portal", lang, [&](Html &out) { web::portalPage(out, nets, nullptr); });
    render(dir, "portal-error", lang,
           [&](Html &out) { web::portalPage(out, nets, web::text(lang, Txt::ErrWifiPass)); });
    render(dir, "update", lang, [&](Html &out) { web::updatePage(out, "CSRFTOKEN"); });
    String err;
    err += web::text(lang, Txt::ErrUrl);
    err += web::text(lang, Txt::ErrTz);
    render(dir, "save-error", lang, [&](Html &out) { web::errorPage(out, nullptr, err); });
    render(dir, "update-failed", lang,
           [&](Html &out) { web::errorPage(out, web::text(lang, Txt::UpdateFailed), "Not Enough Space"); });
    render(dir, "message", lang, [&](Html &out) { web::messagePage(out, Txt::WifiForgotten, false, false); });
  }
  check(page.find("cafe &lt;guest&gt;") == std::string::npos, "message page carries no network list");
}

void escaping(const std::string &dir) {
  std::puts("escaping: settings and network names are HTML-escaped");
  Settings s;
  s.usageUrl = "https://x/'><script>";
  UsageState u;
  const web::DeviceStatus dev{"a<b", -60, "10.0.0.2", 0, 30000, 0};
  render(dir, "escape", Lang::En, [&](Html &out) { web::adminPage(out, s, u, dev, "T"); });
  check(page.find("<script>") == std::string::npos, "URL is escaped");
  check(page.find("https://x/&#39;&gt;&lt;script&gt;") != std::string::npos, "escaped URL is present");
  check(page.find("a&lt;b") != std::string::npos, "SSID is escaped");
  check(page.find(web::text(Lang::En, Txt::NotSynced)) != std::string::npos, "unsynced clock is shown");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <out-dir>\n", argv[0]);
    return 2;
  }
  setenv("TZ", "KST-9", 1);
  tzset();
  textTable();
  pages(argv[1]);
  escaping(argv[1]);
  std::printf("%d failures\n", failures);
  return failures ? 1 : 0;
}
