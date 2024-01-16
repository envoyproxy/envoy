#include "test/common/http/filters/test_remote_response/config.h"

#include "test/common/http/filters/test_remote_response/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestRemoteResponse {

Http::FilterFactoryCb TestRemoteResponseFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_remote_response::TestRemoteResponse&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TestRemoteResponseFilter>());
  };
}

/**
 * Static registration for the TestRemoteResponse filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(TestRemoteResponseFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestRemoteResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
