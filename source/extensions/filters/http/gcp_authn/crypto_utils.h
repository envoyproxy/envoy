#pragma once

#include <string>

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class CertFingerprinter {
public:
  virtual ~CertFingerprinter() = default;
  virtual absl::StatusOr<std::string> getFingerprintFromPem(const std::string& pem) const = 0;
};
using CertFingerprinterSharedPtr = std::shared_ptr<const CertFingerprinter>;

class CertFingerprinterImpl : public CertFingerprinter {
public:
  absl::StatusOr<std::string> getFingerprintFromPem(const std::string& pem) const override;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
