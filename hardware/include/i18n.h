#pragma once
#include <Arduino.h>

#include "settings.h"

// Text tables are X(id, English, Korean) lists expanded by these macros into PROGMEM strings and a [id][lang] table.
#define I18N_ID(id, en, ko) id,
#define I18N_STRINGS(id, en, ko) \
  const char id##_en[] PROGMEM = en; \
  const char id##_ko[] PROGMEM = ko;
#define I18N_ROW(id, en, ko) {id##_en, id##_ko},

static_assert(LANG_COUNT == 2, "every I18N list carries one column per language");

// Clock screen text. Entries with a % are printf formats.
#define SCREEN_TEXT(X)                                                    \
  X(SyncingTime, "Syncing time", "시간 동기화 중")                         \
  X(Last7Days, "Last 7 days", "최근 7일 사용량")                           \
  X(ConnectingWifi, "Connecting Wi-Fi", "Wi-Fi 연결 중")                   \
  X(WifiLost, "Wi-Fi offline", "Wi-Fi 연결 끊김")                          \
  X(Resets, "Reset %s", "초기화 %s")                                      \
  X(ResetsAndStatus, "Reset %s \xC2\xB7 %s", "초기화 %s \xC2\xB7 %s")       \
  X(MinutesAgo, "%lum ago", "%lu분 전")                                   \
  X(HoursAgo, "%luh ago", "%lu시간 전")                                   \
  X(DaysAgo, "%lud ago", "%lu일 전")                                      \
  X(AsOf, "as of %.5s", "%.5s 기준")                                      \
  X(NoUrl, "No URL set", "URL 미설정")                                    \
  X(WaitingForTime, "Waiting for time", "시각 동기화 대기")                \
  X(Loading, "Loading", "불러오는 중")                                    \
  X(Failed, "Failed", "실패")                                             \
  X(FailedAge, "Failed \xC2\xB7 %s", "실패 \xC2\xB7 %s")                    \
  X(Stale, "Stale", "지연")                                               \
  X(StaleAsOf, "Stale \xC2\xB7 as of %.5s", "지연 \xC2\xB7 %.5s 기준")       \
  X(ServerError, "Server error", "서버 오류")                             \
  X(ServerErrorCode, "Server error %s", "서버 오류 %s")                   \
  X(NoCertCheck, "No cert check", "인증서 검증 안 함")                     \
  X(TokenPlaintext, "Plaintext token", "토큰 평문 전송")                   \
  X(SetupMode, "Setup mode", "설정 모드")                                 \
  X(Password, "Password", "비밀번호")                                     \
  X(OpenAddress, "Open  http://192.168.4.1", "접속 주소  http://192.168.4.1") \
  X(AdminLogin, "Admin  admin / %s", "관리자  admin / %s")                \
  X(Updating, "Updating", "업데이트 중")                                  \
  X(Done, "Done", "완료")                                                 \
  X(Restarting, "Restarting", "재시작")                                   \
  X(Error, "Error", "오류")

enum class Msg : uint8_t { SCREEN_TEXT(I18N_ID) Count };

namespace i18n {

inline PGM_P pick(const char *const table[][LANG_COUNT], uint8_t id, Lang lang) {
  return reinterpret_cast<PGM_P>(pgm_read_ptr(&table[id][static_cast<uint8_t>(lang)]));
}

PGM_P text(Lang lang, Msg id);
// Copies a clock screen string into RAM, truncating to n - 1 bytes.
void copy(Lang lang, Msg id, char *out, size_t n);

}  // namespace i18n
