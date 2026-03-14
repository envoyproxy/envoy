#pragma once

#include "envoy/extensions/bootstrap/certificate_providers/local/v3/local_certificate_provider.pb.h"
#include "envoy/extensions/bootstrap/certificate_providers/local/v3/local_certificate_provider.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace CertificateProviders {
namespace Local {

class LocalCertificateProviderExtension : public Server::BootstrapExtension,
                                          protected Logger::Loggable<Logger::Id::secret> {
public:
  LocalCertificateProviderExtension(
      const envoy::extensions::bootstrap::certificate_providers::local::v3::LocalCertificateProvider&
          config)
      : provider_name_(config.provider_name()),
        local_signer_config_(config.local_signer()) {}

  void onServerInitialized(Server::Instance& server) override;
  void onWorkerThreadInitialized() override {}

private:
  const std::string provider_name_;
  const envoy::extensions::bootstrap::certificate_providers::local::v3::LocalSigner
      local_signer_config_;
};

class LocalCertificateProviderFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::bootstrap::certificate_providers::local::v3::LocalCertificateProvider>();
  }

  std::string name() const override { return "envoy.bootstrap.certificate_providers.local"; }
};

} // namespace Local
} // namespace CertificateProviders
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
