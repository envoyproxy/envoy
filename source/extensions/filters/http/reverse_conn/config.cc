#include "source/extensions/filters/http/reverse_conn/config.h"

#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

Http::FilterFactoryCb ReverseConnFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::reverse_conn::v3::ReverseConn& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ReverseConnFilterConfigSharedPtr config =
      std::make_shared<ReverseConnFilterConfig>(ReverseConnFilterConfig(proto_config));

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ReverseConnFilter>(config));
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
