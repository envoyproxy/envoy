#pragma once

#include "envoy/extensions/bootstrap/certificate_providers/local/v3/local_certificate_provider.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/factory_context.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace Local {

using LocalSignerProto =
    envoy::extensions::bootstrap::certificate_providers::local::v3::LocalSigner;

class LocalNamedTlsCertificateProvider : public Secret::NamedTlsCertificateProvider {
public:
  explicit LocalNamedTlsCertificateProvider(const LocalSignerProto& config);

  Secret::TlsCertificateConfigProviderSharedPtr
  getProvider(const std::string& certificate_name,
              Server::Configuration::ServerFactoryContext& server_context) override;

  absl::Status refreshProviders();

private:
  const LocalSignerProto config_;
  absl::flat_hash_map<std::string, std::weak_ptr<Secret::TlsCertificateConfigProvider>> providers_;
};

absl::StatusOr<Secret::TlsCertificateConfigProviderSharedPtr>
findOrCreateLocalSignerCertificateProvider(
    absl::string_view secret_name, Server::Configuration::ServerFactoryContext& factory_context,
    const LocalSignerProto& local_signer_config);

absl::Status
refreshLocalSignerCertificateProviders(const LocalSignerProto& local_signer_config);

} // namespace Local
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
