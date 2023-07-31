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
      : preserve_url_encoded_case_(
            snapshot.getBoolean("envoy.uhv.preserve_url_encoded_case", true)) {}

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
