#pragma once

#include "envoy/registry/registry.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/server/factory_context.h"

#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tls/context_impl.h"

#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

#define ALL_CERT_SELECTION_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(cert_selection_sync)                                                                     \
  COUNTER(cert_selection_async)                                                                    \
  COUNTER(cert_selection_async_finished)                                                           \
  COUNTER(cert_selection_sleep)                                                                    \
  COUNTER(cert_selection_sleep_finished)                                                           \
  COUNTER(cert_selection_failed)

struct CertSelectionStats {
  ALL_CERT_SELECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)
};

class OnDemandTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                    protected Logger::Loggable<Logger::Id::connection> {
public:
  OnDemandTlsCertificateSelector(Stats::Scope& store, Ssl::TlsCertificateSelectorContext& selector_ctx);

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO&,
                                        Ssl::CertificateSelectionCallbackPtr) override {
    return Ssl::SelectionResult{};
  }

  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("Not supported with QUIC");
  };

private:
  CertSelectionStats stats_;
};

using ConfigProto = envoy::extensions::certificate_selectors::on_demand_secret::v3::Config;

// Maintains dynamic subscriptions to SDS secrets and converts them from the xDS form to the boringssl TLS contexts,
// while applying the parent TLS configuration.
class SecretManager {
public:
  explicit SecretManager(const ConfigProto& config, Server::Configuration::GenericFactoryContext& factory_context, const Ssl::ContextConfig& tls_config);
private:
  absl::Status loadCerts();

  const std::string cert_name_;
  Server::Configuration::GenericFactoryContext& factory_context_;
  const Ssl::ContextConfig& tls_config_;
  absl::optional<Ssl::TlsCertificateConfigImpl> cert_config_;
  absl::optional<Ssl::TlsContext> cert_context_;
  Secret::TlsCertificateConfigProviderSharedPtr cert_provider_;
  Common::CallbackHandlePtr cert_callback_handle_;
};

class OnDemandTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactory> createTlsCertificateSelectorFactory(
      const Protobuf::Message& proto_config, Server::Configuration::GenericFactoryContext& factory_context,
      const Ssl::ContextConfig& tls_config, bool for_quic) override {
    if (for_quic) {
      return absl::InvalidArgumentError("Does not support QUIC listeners.");
    }
    const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config, factory_context.messageValidationVisitor());
    auto secret_manager = std::make_shared<SecretManager>(config, factory_context, tls_config);
    auto& scope = factory_context.statsScope();
    return [&scope, secret_manager](const Ssl::ServerContextConfig&,
                          Ssl::TlsCertificateSelectorContext& selector_ctx) {
      return std::make_unique<OnDemandTlsCertificateSelector>(scope, selector_ctx);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return "envoy.certificate_selectors.on_demand_secret"; }
};

DECLARE_FACTORY(OnDemandTlsCertificateSelectorFactory);


} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
