#include "source/extensions/transport_sockets/tls/cert_mappers/filter_state_override/config.h"

#include "envoy/router/string_accessor.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateMappers {
namespace FilterStateOverride {

namespace {
class Mapper : public Ssl::UpstreamTlsCertificateMapper {
public:
  explicit Mapper(const std::string& default_value) : default_value_(default_value) {}
  std::string deriveFromServerHello(const SSL&,
                                    const Network::TransportSocketOptionsConstSharedPtr& options) {
    if (options) {
      const StreamInfo::FilterStateObjectsSharedPtr& objects =
          options->downstreamSharedFilterStateObjects();
      const auto* data = objects ? objects->getDataReadOnly<Router::StringAccessor>(
                                       "envoy.tls.certificate_mappers.on_demand_secret")
                                 : nullptr;
      if (data) {
        return std::string(data->asString());
      }
    }
    return default_value_;
  }

private:
  const std::string default_value_;
};
} // namespace

absl::StatusOr<Ssl::UpstreamTlsCertificateMapperFactory>
MapperFactory::createTlsCertificateMapperFactory(
    const Protobuf::Message& proto_config,
    Server::Configuration::GenericFactoryContext& factory_context) {
  const ConfigProto& config = MessageUtil::downcastAndValidate<const ConfigProto&>(
      proto_config, factory_context.messageValidationVisitor());
  return [default_value = config.default_value()]() {
    return std::make_unique<Mapper>(default_value);
  };
}

REGISTER_FACTORY(MapperFactory, Ssl::UpstreamTlsCertificateMapperConfigFactory);

} // namespace FilterStateOverride
} // namespace CertificateMappers
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
