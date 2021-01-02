#include "extensions/filters/http/tcp_post/config.h"

#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.h"
#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/tcp_post/tcp_post_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {

Http::FilterFactoryCb TcpPostFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::tcp_post::v3::TcpPost& proto_config, const std::string&,
    Server::Configuration::FactoryContext& context) {
  return [proto_config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TcpPostFilter>(proto_config));
  };
}

/**
 * Static registration for the TcpPost filter. @see RegisterFactory.
 */
REGISTER_FACTORY(TcpPostFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
