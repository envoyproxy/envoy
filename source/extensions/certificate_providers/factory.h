#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/certificate_providers/certificate_provider.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {

class CertificateProviderFactory : public Config::TypedFactory {
public:
  virtual Envoy::Extensions::CertificateProviders::CertificateProviderSharedPtr
  createCertificateProviderInstance(const envoy::config::core::v3::TypedExtensionConfig& config,
                                    Api::Api& api) PURE;

  std::string category() const override { return "envoy.certificate_providers"; }
};

} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
