#pragma once

#include <openssl/ssl.h>

#include "envoy/ssl/handshaker.h"

#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_impl.h"

#include "stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class AsyncTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                    protected Logger::Loggable<Logger::Id::connection> {
public:
  AsyncTlsCertificateSelector(Stats::Scope& store, Ssl::TlsCertificateSelectorContext& selector_ctx,
                              std::string mode)
      : stats_(generateCertSelectionStats(store)), selector_ctx_(selector_ctx), mode_(mode) {}

  ~AsyncTlsCertificateSelector() override {
    ENVOY_LOG(info, "debug: ~AsyncTlsCertificateSelector");
  }

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO&,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  // It's only for quic.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
  findTlsContext(absl::string_view, const Ssl::CurveNIDVector&, bool, bool*) override {
    PANIC("unreachable");
  };

  void selectTlsContextAsync();

private:
  CertSelectionStats stats_;
  Ssl::TlsCertificateSelectorContext& selector_ctx_;
  Ssl::CertificateSelectionCallbackPtr cb_;
  std::string mode_;
  Event::TimerPtr selection_timer_;
};

class AsyncTlsFactory : public Ssl::TlsCertificateSelectorFactory {
public:
  AsyncTlsFactory(const std::string& mode, Stats::Scope& scope) : mode_(mode), scope_(scope) {}
  Ssl::TlsCertificateSelectorPtr create(Ssl::TlsCertificateSelectorContext& selector_ctx) override {
    return std::make_unique<AsyncTlsCertificateSelector>(scope_, selector_ctx, mode_);
  };
  absl::Status onConfigUpdate() override { return absl::OkStatus(); }

private:
  const std::string mode_;
  Stats::Scope& scope_;
};

class AsyncTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  absl::StatusOr<Ssl::TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const Ssl::ServerContextConfig&, bool for_quic) override {
    if (for_quic) {
      return absl::InvalidArgumentError("does not support for quic");
    }

    auto& string_value = dynamic_cast<const Protobuf::StringValue&>(config);
    std::string mode = string_value.value();
    if (mode.empty()) {
      return absl::InvalidArgumentError("invalid cert selection mode");
    }

    auto& scope = factory_context.serverFactoryContext().scope();
    return std::make_unique<AsyncTlsFactory>(mode, scope);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Protobuf::StringValue()};
  }

  std::string name() const override { return "test-tls-context-provider"; };
};

DECLARE_FACTORY(AsyncTlsCertificateSelectorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
