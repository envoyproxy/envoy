#include "source/extensions/filters/http/rfc9440_client_cert/config.h"

#include "source/extensions/filters/http/rfc9440_client_cert/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

Http::FilterFactoryCb Rfc9440ClientCertFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rfc9440_client_cert::v3::Rfc9440ClientCert&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto filter_config =
      std::make_shared<Rfc9440ClientCertFilterConfig>(proto_config.set_client_cert_chain());

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Rfc9440ClientCertFilter>(filter_config));
  };
}

REGISTER_FACTORY(Rfc9440ClientCertFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
