#include "source/server/ssl_context_manager.h"

#include <cstddef>

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Server {

/**
 * A stub that provides a SSL context manager capable of reporting on
 * certificates' data in case there's no TLS implementation built
 * into Envoy.
 */
class SslContextManagerNoTlsStub final : public Envoy::Ssl::ContextManager {
  Ssl::ClientContextSharedPtr
  createSslClientContext(Stats::Scope& /* scope */,
                         const Envoy::Ssl::ClientContextConfig& /* config */) override {
    throwException();
  }

  Ssl::ServerContextSharedPtr
  createSslServerContext(Stats::Scope& /* scope */,
                         const Envoy::Ssl::ServerContextConfig& /* config */,
                         const std::vector<std::string>& /* server_names */) override {
    throwException();
  }

  absl::optional<uint32_t> daysUntilFirstCertExpires() const override {
    return absl::make_optional(std::numeric_limits<uint32_t>::max());
  }
  absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override {
    return absl::nullopt;
  }

  void iterateContexts(std::function<void(const Envoy::Ssl::Context&)> /* callback */) override{};

  Ssl::PrivateKeyMethodManager& privateKeyMethodManager() override { throwException(); }

  void removeContext(const Envoy::Ssl::ContextSharedPtr& old_context) override {
    if (old_context) {
      throwEnvoyExceptionOrPanic("SSL is not supported in this configuration");
    }
  }

private:
  [[noreturn]] void throwException() {
    throwEnvoyExceptionOrPanic("SSL is not supported in this configuration");
  }
};

Ssl::ContextManagerPtr createContextManager(const std::string& factory_name,
                                            TimeSource& time_source) {
  Ssl::ContextManagerFactory* factory =
      Registry::FactoryRegistry<Ssl::ContextManagerFactory>::getFactory(factory_name);
  if (factory != nullptr) {
    return factory->createContextManager(time_source);
  }

  return std::make_unique<SslContextManagerNoTlsStub>();
}

} // namespace Server
} // namespace Envoy
