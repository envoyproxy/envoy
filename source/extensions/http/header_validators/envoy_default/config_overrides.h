#pragma once

#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

struct ConfigOverrides {
  ConfigOverrides() = default;
  ConfigOverrides(const Envoy::Runtime::Snapshot& snapshot)
      : reject_percent_00_(snapshot.getBoolean("envoy.uhv.reject_percent_00", true)),
        preserve_url_encoded_case_(
            snapshot.getBoolean("envoy.uhv.preserve_url_encoded_case", true)) {}

  // This flag enables check for the %00 sequence in the URL path. If this sequence is
  // found request is rejected as invalid. This check requires path normalization to be
  // enabled to occur.
  // https://datatracker.ietf.org/doc/html/rfc3986#section-2.1 allows %00 sequence, and
  // this check is implemented for backward compatibility with legacy path normalization
  // only.
  //
  // This option currently is `true` by default and can be overridden using the
  // "envoy.uhv.reject_percent_00" runtime value. Note that the default value
  // will be changed to `false` in the future to make it RFC compliant.
  const bool reject_percent_00_{true};

  // This flag enables preservation of the case of percent-encoded triplets in URL path for
  // compatibility with legacy path normalization.
  // https://datatracker.ietf.org/doc/html/rfc3986#section-2.1 mandates that uppercase
  // hexadecimal digits (A through F) are equivalent to lowercase.
  // However to make path matching of percent-encoded triplets easier path normalization changes all
  // hexadecimal digits to uppercase.
  //
  // This option currently is `true` by default and can be overridden using the
  // "envoy.uhv.preserve_url_encoded_case" runtime value. Note that the default value
  // will be changed to `false` in the future to make it easier to write path matchers that
  // look for percent-encoded triplets.
  const bool preserve_url_encoded_case_{true};
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
