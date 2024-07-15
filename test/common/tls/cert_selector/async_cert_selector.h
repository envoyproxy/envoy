#pragma once

#include <openssl/ssl.h>

#include "envoy/ssl/handshaker.h"

#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class AsyncTlsCertificateSelector : public Ssl::TlsCertificateSelector,
                                    protected Logger::Loggable<Logger::Id::connection> {
public:
  AsyncTlsCertificateSelector(Ssl::TlsCertificateSelectorContext& selector_ctx, std::string mode)
      : selector_ctx_(selector_ctx), mode_(mode) {}

  ~AsyncTlsCertificateSelector() override {
    ENVOY_LOG(info, "debug: ~AsyncTlsCertificateSelector");
  }

  Ssl::SelectionResult selectTlsContext(const SSL_CLIENT_HELLO&,
                                        Ssl::CertificateSelectionCallbackPtr cb) override;

  // It's only for quic.
  std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction> findTlsContext(absl::string_view, bool,
                                                                          bool, bool*) override {
    PANIC("unreachable");
  };

  void selectTlsContextAsync();

private:
  Ssl::TlsCertificateSelectorContext& selector_ctx_;
  Ssl::CertificateSelectionCallbackPtr cb_;
  std::string mode_;
  Event::TimerPtr selection_timer_;
};

class AsyncTlsCertificateSelectorFactory : public Ssl::TlsCertificateSelectorConfigFactory {
public:
  Ssl::TlsCertificateSelectorFactory createTlsCertificateSelectorFactory(
      const Protobuf::Message& config, Server::Configuration::CommonFactoryContext&,
      ProtobufMessage::ValidationVisitor&, absl::Status& creation_status, bool for_quic) override {
    if (for_quic) {
      creation_status = absl::InvalidArgumentError("does not support for quic");
      return Ssl::TlsCertificateSelectorFactory();
    }

    std::string mode;
    const ProtobufWkt::Any* any_config = dynamic_cast<const ProtobufWkt::Any*>(&config);
    if (any_config) {
      ProtobufWkt::StringValue string_value;
      if (any_config->UnpackTo(&string_value)) {
        mode = string_value.value();
      }
    }
    if (mode.empty()) {
      creation_status = absl::InvalidArgumentError("invalid cert selection mode");
      return Ssl::TlsCertificateSelectorFactory();
    }

    return
        [mode](const Ssl::ServerContextConfig&, Ssl::TlsCertificateSelectorContext& selector_ctx) {
          return std::make_unique<AsyncTlsCertificateSelector>(selector_ctx, mode);
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ProtobufWkt::StringValue()};
  }

  std::string name() const override { return "test-tls-context-provider"; };
};

DECLARE_FACTORY(AsyncTlsCertificateSelectorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
