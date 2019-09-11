#include "envoy/registry/registry.h"
#include "envoy/ssl/context_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

/**
 * A factory to create server side TLS context config from a protobuf.
 */
class DownstreamContextConfigFactory : public Ssl::ContextConfigFactory {
public:
  ~DownstreamContextConfigFactory() override = default;

  Ssl::ContextConfigPtr
  createSslContextConfig(const Protobuf::Message& config,
                         Server::Configuration::TransportSocketFactoryContext& context) override;

  std::string name() const override { return "downstream_tls_context"; }
};

DECLARE_FACTORY(DownstreamContextConfigFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
