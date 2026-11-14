#include "usage.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <memory>

#include "ca_roots.h"

namespace usage {
namespace {

bool hostAndPort(const String &url, String &host, uint16_t &port) {
  int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) return false;
  port = url.startsWith("https") ? 443 : 80;
  int start = schemeEnd + 3;
  int end = url.indexOf('/', start);
  String authority = url.substring(start, end < 0 ? url.length() : end);
  int at = authority.lastIndexOf('@');
  if (at >= 0) authority = authority.substring(at + 1);
  int colon = authority.lastIndexOf(':');
  if (colon >= 0) {
    port = authority.substring(colon + 1).toInt();
    authority = authority.substring(0, colon);
  }
  host = authority;
  return host.length() > 0 && port > 0;
}

bool fail(UsageState &state, const String &why) {
  state.lastOk = false;
  state.lastError = why;
  return false;
}

// Records the free heap once the TLS client and its buffers are released.
struct HeapAfterFetch {
  TlsStats &tls;
  ~HeapAfterFetch() {
    if (tls.heapBefore) tls.heapAfter = ESP.getFreeHeap();
  }
};

// Loads the built-in root certificates; returns null if any of them fails to parse.
std::unique_ptr<BearSSL::X509List> loadCaRoots() {
  std::unique_ptr<BearSSL::X509List> list(new BearSSL::X509List());
  for (size_t i = 0; i < CA_ROOT_COUNT; i++) {
    if (!list->append(CA_ROOTS[i].der, CA_ROOTS[i].len)) return nullptr;
  }
  return list;
}

}  // namespace

bool fetch(const Settings &s, UsageState &state) {
  state.lastAttemptMs = millis();
  if (s.usageUrl.isEmpty()) return fail(state, F("URL not set"));
  if (WiFi.status() != WL_CONNECTED) return fail(state, F("Wi-Fi down"));

  state.tls = TlsStats();
  HeapAfterFetch heapAfter{state.tls};
  std::unique_ptr<BearSSL::X509List> roots;
  std::unique_ptr<WiFiClient> client;
  BearSSL::WiFiClientSecure *secure = nullptr;
  if (settings::isHttps(s)) {
    state.tls.heapBefore = ESP.getFreeHeap();
    state.tls.maxBlockBefore = ESP.getMaxFreeBlockSize();
    secure = new BearSSL::WiFiClientSecure();
    client.reset(secure);
    switch (s.tlsMode) {
      case TlsMode::Ca:
        if (time(nullptr) < VALID_EPOCH) return fail(state, F("clock not synced"));
        roots = loadCaRoots();
        if (!roots) return fail(state, F("CA roots unavailable"));
        secure->setTrustAnchors(roots.get());
        secure->setX509Time(time(nullptr));
        break;
      case TlsMode::Fingerprint:
        if (!s.tlsFingerprint.length()) return fail(state, F("fingerprint not set"));
        if (!secure->setFingerprint(s.tlsFingerprint.c_str())) return fail(state, F("bad fingerprint"));
        break;
      case TlsMode::Insecure:
        secure->setInsecure();
        break;
    }
    String host;
    uint16_t port;
    const bool mfln = hostAndPort(s.usageUrl, host, port) &&
                      BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, port, 1024);
    state.tls.mfln = mfln ? 1 : 0;
    if (mfln) secure->setBufferSizes(1024, 1024);
    else secure->setBufferSizes(16384, 512);
  } else if (s.usageUrl.startsWith("http://")) {
    client.reset(new WiFiClient());
  } else {
    return fail(state, F("URL must start with http:// or https://"));
  }

  HTTPClient http;
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  if (!http.begin(*client, s.usageUrl)) return fail(state, F("bad URL"));
  if (s.usageToken.length()) http.addHeader(F("Authorization"), String(F("Bearer ")) + s.usageToken);
  http.addHeader(F("Accept"), F("application/json"));

  int code = http.GET();
  if (secure) {
    state.tls.heapConnected = ESP.getFreeHeap();
    state.tls.sslError = secure->getLastSSLError();
  }
  if (code != HTTP_CODE_OK) {
    String why = code < 0 ? HTTPClient::errorToString(code) : String(F("HTTP ")) + code;
    if (code < 0 && state.tls.sslError) why += String(F(" (TLS ")) + state.tls.sslError + ')';
    http.end();
    return fail(state, why);
  }
  int size = http.getSize();
  if (size > static_cast<int>(MAX_BODY)) {
    http.end();
    return fail(state, F("response too large"));
  }
  String body = http.getString();
  http.end();
  if (body.length() > MAX_BODY) return fail(state, F("response too large"));

  UsageState parsed = state;
  if (!parse(body.c_str(), body.length(), parsed)) return fail(state, F("invalid JSON"));
  parsed.haveData = true;
  parsed.lastOk = true;
  parsed.lastSuccessMs = millis();
  parsed.lastError = "";
  state = parsed;
  return true;
}

}  // namespace usage
