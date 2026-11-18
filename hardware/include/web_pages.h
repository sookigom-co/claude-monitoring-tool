#pragma once
#include <Arduino.h>
#include <time.h>

#include "i18n.h"
#include "settings.h"
#include "usage.h"

// Web page text. Entries ending in Help are the short explanations shown under each setting.
#define WEB_TEXT(X)                                                                                                  \
  X(Back, "Back", "돌아가기")                                                                                         \
  X(Firmware, "Firmware", "펌웨어")                                                                                   \
  X(Time, "Time", "시각")                                                                                             \
  X(FreeHeap, "Free heap", "남은 메모리 (heap)")                                                                       \
  X(UsageFeed, "Usage feed", "사용량 정보")                                                                           \
  X(CertCheck, "Certificate check", "인증서 검사")                                                                    \
  X(NotSynced, "not synced", "동기화 안 됨")                                                                          \
  X(UrlNotSet, "URL not set", "URL 미설정")                                                                           \
  X(RelayStale, "relay stale: ", "중계 서버 지연: ")                                                                   \
  X(RelayError, "relay error: ", "중계 서버 오류: ")                                                                   \
  X(StatusOk, "OK, ", "정상, ")                                                                                       \
  X(MinutesAgo, " min ago", "분 전")                                                                                  \
  X(StatusFailed, "failed: ", "실패: ")                                                                               \
  X(Pending, "pending", "대기 중")                                                                                    \
  X(WarnNoCertCheck,                                                                                                 \
    "Warning: the certificate is not checked, so anyone on the path can read the Bearer token. Use CA mode.",       \
    "경고: 인증서를 검사하지 않으므로 경로 중간의 누구나 Bearer 토큰을 읽을 수 있습니다. CA 방식을 쓰세요.")           \
  X(WarnPlaintext, "Warning: http:// sends the Bearer token in plain text. Use an https:// URL with CA mode.",        \
    "경고: http:// 는 Bearer 토큰을 평문으로 보냅니다. https:// 주소와 CA 방식을 쓰세요.")                            \
  X(LangHelp, "Language of this page and of the clock screen. It changes as soon as you save.",                     \
    "이 설정 페이지와 시계 화면에 쓰는 언어입니다. 저장하면 바로 바뀝니다.")                                          \
  X(UrlLabel, "JSON URL (http:// or https://)", "JSON 주소 (http:// 또는 https://)")                                  \
  X(UrlHelp,                                                                                                         \
    "Address of the relay server that reports usage, e.g. https://clock.example.com/clock/usage. Leave empty to "  \
    "show the clock only.",                                                                                          \
    "사용량을 알려 주는 중계 서버 주소입니다. 예: https://clock.example.com/clock/usage. 비워 두면 시계만 표시합니다.")  \
  X(TokenLabel, "Bearer token", "Bearer 토큰")                                                                        \
  X(TokenHelp, "Sent in the Authorization header. Leave empty to keep the saved token.",                             \
    "Authorization 헤더로 보내는 값입니다. 비워 두면 저장된 토큰을 그대로 씁니다.")                                    \
  X(TokenSet, "set - leave empty to keep", "설정됨 - 비워 두면 유지")                                                 \
  X(TokenNotSet, "not set", "설정 안 됨")                                                                             \
  X(ClearToken, "clear token", "토큰 지우기")                                                                         \
  X(ClearTokenHelp, "Deletes the saved token when you save.", "저장할 때 저장된 토큰을 지웁니다.")                       \
  X(TlsLabel, "HTTPS certificate check", "HTTPS 인증서 검사")                                                         \
  X(TlsHelp, "How the clock checks the server certificate of an https:// URL. CA suits most servers.",               \
    "https:// 주소의 서버 인증서를 확인하는 방식입니다. 대부분의 서버는 CA 방식이면 됩니다.")                           \
  X(TlsCa, "CA (Let's Encrypt ISRG Root X1/X2) - default", "CA (Let's Encrypt ISRG Root X1/X2) - 기본값")              \
  X(TlsFingerprint, "Pinned SHA-1 fingerprint", "SHA-1 지문 고정")                                                    \
  X(TlsInsecure, "No check (insecure)", "검사 안 함 (안전하지 않음)")                                                 \
  X(FpLabel, "TLS SHA-1 fingerprint (fingerprint mode only)", "TLS SHA-1 지문 (지문 방식에서만 사용)")                \
  X(FpHelp, "40 hex digits; colons and spaces are allowed. Update it whenever the server certificate is renewed.",   \
    "16진수 40자리이며 콜론과 공백은 넣어도 됩니다. 서버 인증서가 갱신될 때마다 바꿔야 합니다.")                        \
  X(RefreshLabel, "Refresh interval (minutes)", "갱신 간격 (분)")                                                     \
  X(RefreshHelp, "How often usage is fetched, 1-1440 minutes. After a failure the clock retries within 2 minutes.",  \
    "사용량을 가져오는 주기이며 1~1440분입니다. 실패하면 2분 안에 다시 시도합니다.")                                   \
  X(DisplayLegend, "Display", "화면")                                                                                 \
  X(TzLabel, "POSIX time zone", "POSIX 시간대")                                                                       \
  X(TzHelp, "Time zone rule in POSIX TZ format, e.g. KST-9 (Korea), UTC0, EST5EDT,M3.2.0,M11.1.0 (US Eastern).",     \
    "POSIX TZ 형식의 시간대 규칙입니다. 예: KST-9 (한국), UTC0, EST5EDT,M3.2.0,M11.1.0 (미국 동부).")                   \
  X(BrightLabel, "Brightness (0-100)", "밝기 (0~100)")                                                                \
  X(BrightHelp, "Backlight level in percent. 0 turns the backlight off.", "백라이트 밝기(%)입니다. 0이면 백라이트가 꺼집니다.") \
  X(LayoutLabel, "Screen design", "화면 디자인")                                                                      \
  X(LayoutHelp, "Arrangement of the clock and the two usage gauges. D and E show usage only, without the clock. E needs relay 1.3.0 or later.",          \
    "시계와 사용량 게이지 2개의 배치입니다. D와 E는 시계 없이 사용량만 보여 줍니다. E는 relay 1.3.0 이상이 필요합니다.")                                       \
  X(LayoutA, "A - concentric rings (Claude outside, Fable inside)", "A - 동심원 링 (바깥 Claude, 안쪽 Fable)")          \
  X(LayoutB, "B - clock on top, two gauges below", "B - 위에 시계, 아래에 게이지 2개")                                   \
  X(LayoutC, "C - left/right half rings (default)", "C - 좌우 반원 링 (기본값)")                                       \
  X(LayoutD, "D - usage only, large numbers and bars (no clock)", "D - 사용량만, 큰 숫자와 막대 (시계 없음)")            \
  X(LayoutE, "E - usage only, hourly increase over the last 24 hours (no clock)",                                     \
    "E - 사용량만, 최근 24시간 시간별 증가량 (시계 없음)")                                                              \
  X(ModeLabel, "Design change", "디자인 전환")                                                                        \
  X(ModeHelp, "Fixed keeps one design on screen. Cycle shows A, B, C, D and E in turn.",                               \
    "고정은 디자인 하나만 보여 주고, 순환은 A, B, C, D, E를 차례로 보여 줍니다.")                                         \
  X(ModeFixed, "Fixed - always show the design above", "고정 - 위에서 고른 디자인만 표시")                              \
  X(ModeCycle, "Cycle - A, B, C, D, E in turn, starting from the design above",                                       \
    "순환 - 위에서 고른 디자인부터 A, B, C, D, E 차례로")                                                                 \
  X(CycleLabel, "Cycle interval (seconds, 5-300)", "순환 간격 (초, 5~300)")                                            \
  X(CycleHelp, "How long each design stays on screen in cycle mode.", "순환 모드에서 디자인 하나를 보여 주는 시간입니다.") \
  X(AdminLegend, "Admin password (also OTA password)", "관리자 비밀번호 (OTA 비밀번호 겸용)")                            \
  X(NewPassLabel, "New password (8+ chars, empty = keep)", "새 비밀번호 (8자 이상, 비우면 유지)")                        \
  X(NewPassHelp,                                                                                                     \
    "Password for this page (user name admin) and for network firmware updates. The clock restarts after a change.", \
    "이 페이지(사용자 이름 admin)와 네트워크 펌웨어 업데이트에 쓰는 비밀번호입니다. 바꾸면 시계가 재시작합니다.")       \
  X(RepeatLabel, "Repeat", "다시 입력")                                                                               \
  X(Save, "Save", "저장")                                                                                             \
  X(Maintenance, "Maintenance", "유지 관리")                                                                          \
  X(FirmwareUpdate, "Firmware update", "펌웨어 업데이트")                                                             \
  X(Reboot, "Reboot", "재시작")                                                                                       \
  X(WifiReset, "Forget Wi-Fi and open setup portal", "Wi-Fi 정보 지우고 설정 포털 열기")                                \
  X(FactoryReset, "Factory reset (Wi-Fi + all settings)", "공장 초기화 (Wi-Fi + 모든 설정)")                            \
  X(ErrUrl, "URL must start with http:// or https://. ", "URL은 http:// 또는 https:// 로 시작해야 합니다. ")             \
  X(ErrFingerprint, "Fingerprint must be 40 hex digits. ", "지문은 16진수 40자리여야 합니다. ")                         \
  X(ErrTlsMode, "Unknown certificate check mode. ", "알 수 없는 인증서 검사 방식입니다. ")                              \
  X(ErrNeedFingerprint, "Fingerprint mode needs a fingerprint. ", "지문 방식에는 지문이 필요합니다. ")                  \
  X(ErrTz, "Invalid time zone. ", "시간대 형식이 올바르지 않습니다. ")                                                  \
  X(ErrBrightness, "Brightness must be 0-100. ", "밝기는 0~100이어야 합니다. ")                                         \
  X(ErrRefresh, "Refresh must be 1-1440. ", "갱신 간격은 1~1440분이어야 합니다. ")                                      \
  X(ErrLayout, "Unknown screen design. ", "알 수 없는 화면 디자인입니다. ")                                             \
  X(ErrLayoutMode, "Unknown design change mode. ", "알 수 없는 디자인 전환 방식입니다. ")                               \
  X(ErrCycle, "Cycle interval must be 5-300 seconds. ", "순환 간격은 5~300초여야 합니다. ")                             \
  X(ErrLang, "Unknown language. ", "알 수 없는 언어입니다. ")                                                           \
  X(ErrPassMismatch, "Passwords do not match. ", "비밀번호가 서로 다릅니다. ")                                          \
  X(ErrPassShort, "Password too short. ", "비밀번호가 너무 짧습니다. ")                                                 \
  X(ErrFlash, "Saving to flash failed.", "플래시에 저장하지 못했습니다.")                                               \
  X(SavedRestart, "Saved. Restarting to apply the new password.", "저장했습니다. 새 비밀번호를 적용하려고 재시작합니다.") \
  X(BadToken, "Invalid form token. Reload the page.", "폼 토큰이 올바르지 않습니다. 페이지를 새로 고치세요.")            \
  X(BadUploadToken, "Invalid form token. Reload the update page.",                                                   \
    "폼 토큰이 올바르지 않습니다. 업데이트 페이지를 새로 고치세요.")                                                   \
  X(Restarting, "Restarting.", "재시작합니다.")                                                                       \
  X(WifiForgotten, "Wi-Fi forgotten. The setup portal opens after restart; its details are shown on the clock.",      \
    "Wi-Fi 정보를 지웠습니다. 재시작하면 설정 포털이 열리고 접속 정보는 시계 화면에 나옵니다.")                          \
  X(FactoryResetDone, "Factory reset. New passwords are shown on the clock after restart.",                          \
    "공장 초기화합니다. 재시작하면 새 비밀번호가 시계 화면에 나옵니다.")                                                \
  X(Current, "Current: ", "현재 버전: ")                                                                              \
  X(UpdateHelp, "Choose a firmware .bin built for this clock. Settings are kept.",                                   \
    "이 시계용으로 빌드한 펌웨어 .bin 파일을 고르세요. 설정은 유지됩니다.")                                              \
  X(UploadFlash, "Upload and flash", "올려서 설치")                                                                   \
  X(UpdateFailed, "Update failed: ", "업데이트 실패: ")                                                               \
  X(UpdateDone, "Update complete. Restarting.", "업데이트를 마쳤습니다. 재시작합니다.")                                \
  X(PortalTitle, "SmartClock setup", "SmartClock 설정")                                                               \
  X(Network, "Network", "네트워크")                                                                                   \
  X(NetworkHelp, "The 2.4 GHz Wi-Fi network the clock joins. Type the name below if it is hidden.",                 \
    "시계가 연결할 2.4 GHz Wi-Fi 네트워크입니다. 숨긴 네트워크면 아래에 이름을 직접 입력하세요.")                        \
  X(TypeBelow, "(type below)", "(아래에 직접 입력)")                                                                  \
  X(OrSsid, "or SSID", "또는 SSID 직접 입력")                                                                         \
  X(OrSsidHelp, "A typed name wins over the list.", "직접 입력한 이름이 위 목록보다 우선합니다.")                      \
  X(WifiPass, "Wi-Fi password", "Wi-Fi 비밀번호")                                                                     \
  X(WifiPassHelp, "8-63 characters. Leave empty for an open network.", "8~63자입니다. 비밀번호 없는 네트워크면 비워 두세요.") \
  X(PortalAdmin, "New admin password (8+ chars, empty = keep the one shown on the clock)",                           \
    "새 관리자 비밀번호 (8자 이상, 비우면 시계 화면에 표시된 비밀번호 유지)")                                            \
  X(PortalAdminHelp, "Password for the settings page (user name admin) and for network firmware updates.",           \
    "설정 페이지(사용자 이름 admin)와 네트워크 펌웨어 업데이트에 쓰는 비밀번호입니다.")                                  \
  X(SaveConnect, "Save and connect", "저장하고 연결")                                                                 \
  X(Rescan, "Rescan networks", "네트워크 다시 찾기")                                                                  \
  X(ErrSsid, "SSID is required (max 32 chars).", "SSID를 입력하세요 (최대 32자).")                                     \
  X(ErrWifiPass, "Wi-Fi password must be 8-63 chars or empty.", "Wi-Fi 비밀번호는 8~63자이거나 비어 있어야 합니다.")     \
  X(ErrAdminShort, "Admin password must be 8+ chars.", "관리자 비밀번호는 8자 이상이어야 합니다.")                       \
  X(ErrSaveSettings, "Saving settings failed.", "설정을 저장하지 못했습니다.")                                           \
  X(PortalSaved, "Saved. The clock restarts and joins the network.", "저장했습니다. 시계가 재시작한 뒤 네트워크에 연결합니다.")

namespace web {

enum class Txt : uint8_t { WEB_TEXT(I18N_ID) Count };

PGM_P text(Lang lang, Txt id);

// Builds a page in small chunks and hands each one to a sink, so a whole page never sits in RAM.
class Html {
 public:
  using Sink = void (*)(const String &chunk);
  static constexpr size_t CHUNK = 1024;

  Html(Lang lang, Sink sink);
  Lang lang() const { return lang_; }
  Html &operator+=(const __FlashStringHelper *s);
  Html &operator+=(const char *s);
  Html &operator+=(const String &s);
  Html &operator+=(char c);
  Html &operator+=(int v);
  Html &operator+=(unsigned v);
  Html &operator+=(long v);
  Html &operator+=(unsigned long v);
  Html &operator+=(Txt id);
  // Appends s with the HTML special characters escaped.
  void escaped(const char *s);
  void escaped(const String &s) { escaped(s.c_str()); }
  void flush();

 private:
  void spill();

  Lang lang_;
  Sink sink_;
  String buf_;
};

struct DeviceStatus {
  String ssid;
  int32_t rssi;
  String ip;
  time_t now;
  uint32_t freeHeap;
  uint32_t nowMs;
};

struct NetworkList {
  int count;
  String (*ssid)(int i);
  int32_t (*rssi)(int i);
};

void adminPage(Html &out, const Settings &s, const UsageState &u, const DeviceStatus &dev, const String &csrf);
// message is a web text entry shown as a warning above the form, or nullptr.
void portalPage(Html &out, const NetworkList &nets, PGM_P message);
void updatePage(Html &out, const String &csrf);
void messagePage(Html &out, Txt message, bool warn, bool back);
// detail is escaped and appended after the message; used for collected form errors and update errors.
void errorPage(Html &out, PGM_P prefix, const String &detail);

}  // namespace web
