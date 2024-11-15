#include "library/common/extensions/filters/http/socket_tag/config.h"

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "envoy/network/listen_socket.h"

#include "library/common/extensions/filters/http/socket_tag/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketTag {

Http::FilterFactoryCb SocketTagFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::socket_tag::SocketTag& /*proto_config*/,
    const std::string&, Server::Configuration::FactoryContext& /*context*/) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<SocketTagFilter>());
  };
}

/**
 * Static registration for the SocketTag filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(SocketTagFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SocketTag
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
