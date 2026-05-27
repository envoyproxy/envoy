#pragma once

#include <optional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/common/matchers.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

absl::StatusOr<std::string> getBase64EncodedCertificateFingerprint(
    Secret::TlsCertificateConfigProviderSharedPtr tls_cert_provider,
    const std::vector<Matchers::StringMatcherImpl>& san_matchers, Api::Api& api);

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
