#include "contrib/reverse_connection/filters/http/source/config.h"
#include "contrib/reverse_connection/filters/http/source/reverse_conn_filter.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"


namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

Http::FilterFactoryCb ReverseConnFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::reverse_conn::v3alpha::ReverseConn& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ReverseConnFilterConfigSharedPtr config =
      std::make_shared<ReverseConnFilterConfig>(ReverseConnFilterConfig(proto_config));

  // Retrieve the ReverseConnRegistry singleton and access the thread local slot
  std::shared_ptr<ReverseConnection::ReverseConnRegistry> reverse_conn_registry =
        context.serverFactoryContext().singletonManager().getTyped<ReverseConnection::ReverseConnRegistry>("reverse_conn_registry_singleton");
  if (reverse_conn_registry == nullptr) {
    throw EnvoyException(
      "Cannot create reverse conn http filter. Reverse connection registry not found");
  }

  // The ReverseConnFilter is initialized before the workers are created and therefore only the
  // reverse conn global registry is available.
  return [config, reverse_conn_registry](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ReverseConnFilter>(config, reverse_conn_registry));
  };
}

/**
 * Static registration for the reverse_conn filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<ReverseConnFilterConfigFactory,
                                        Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
