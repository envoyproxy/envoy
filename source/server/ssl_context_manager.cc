#include "server/ssl_context_manager.h"

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

  size_t daysUntilFirstCertExpires() const override { return std::numeric_limits<int>::max(); }

  void iterateContexts(std::function<void(const Envoy::Ssl::Context&)> /* callback */) override{};

  Ssl::PrivateKeyMethodManager& privateKeyMethodManager() override { throwException(); }

private:
  [[noreturn]] void throwException() {
    throw EnvoyException("SSL is not supported in this configuration");
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
