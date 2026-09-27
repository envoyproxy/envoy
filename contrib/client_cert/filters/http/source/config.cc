#include "contrib/client_cert/filters/http/source/config.h"

#include "envoy/registry/registry.h"

#include "contrib/client_cert/filters/http/source/client_cert_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {

absl::StatusOr<Http::FilterFactoryCb>
ClientCertFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig& proto_config,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {
  ClientCertFilterConfigSharedPtr filter_config =
      std::make_shared<ClientCertFilterConfig>(proto_config);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ClientCertFilter>(filter_config));
  };
}

/**
 * Static registration for the client_cert filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ClientCertFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
