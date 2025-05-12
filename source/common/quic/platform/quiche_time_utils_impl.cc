// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "quiche_platform_impl/quiche_time_utils_impl.h"

namespace quiche {

namespace {
// NOLINTNEXTLINE(readability-identifier-naming)
absl::optional<int64_t> QuicheUtcDateTimeToUnixSecondsInner(int year, int month, int day, int hour,
                                                            int minute, int second) {
  const absl::CivilSecond civil_time(year, month, day, hour, minute, second);
  if (second != 60 && (civil_time.year() != year || civil_time.month() != month ||
                       civil_time.day() != day || civil_time.hour() != hour ||
                       civil_time.minute() != minute || civil_time.second() != second)) {
    return absl::nullopt;
  }

  const absl::Time time = absl::FromCivil(civil_time, absl::UTCTimeZone());
  return absl::ToUnixSeconds(time);
}
} // namespace

absl::optional<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(int year, int month, int day, int hour,
                                                           int minute, int second) {
  // Handle leap seconds without letting any other irregularities happen.
  if (second == 60) {
    // NOLINTNEXTLINE(readability-identifier-naming)
    auto previous_second =
        QuicheUtcDateTimeToUnixSecondsInner(year, month, day, hour, minute, second - 1);
    if (!previous_second.has_value()) {
      return absl::nullopt;
    }
    return *previous_second + 1;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  return QuicheUtcDateTimeToUnixSecondsInner(year, month, day, hour, minute, second);
}

} // namespace quiche
