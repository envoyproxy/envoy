// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quiche_time_utils_impl.h"

namespace quiche {

namespace {
QuicheOptional<int64_t> quicheUtcDateTimeToUnixSecondsInner(int year, int month, int day, int hour,
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

// NOLINTNEXTLINE(readability-identifier-naming)
QuicheOptional<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(int year, int month, int day, int hour,
                                                           int minute, int second) {
  // Handle leap seconds without letting any other irregularities happen.
  if (second == 60) {
    auto previous_second =
        quicheUtcDateTimeToUnixSecondsInner(year, month, day, hour, minute, second - 1);
    if (!previous_second.has_value()) {
      return absl::nullopt;
    }
    return *previous_second + 1;
  }

  return quicheUtcDateTimeToUnixSecondsInner(year, month, day, hour, minute, second);
}

} // namespace quiche
