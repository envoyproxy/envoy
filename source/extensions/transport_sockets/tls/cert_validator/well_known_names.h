#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * Well-known certificate validator's names.
 */
class CertValidatorValues {
public:
  // default certificate validator
  const std::string Default = "envoy.tls.cert_validator.default";

  // SPIFFE(https://github.com/spiffe/spiffe)
  const std::string SPIFFE = "envoy.tls.cert_validator.spiffe";
};

using CertValidatorNames = ConstSingleton<CertValidatorValues>;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
