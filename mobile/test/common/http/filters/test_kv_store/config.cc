#include "test/common/http/filters/test_kv_store/config.h"

#include "test/common/http/filters/test_kv_store/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestKeyValueStore {

Http::FilterFactoryCb TestKeyValueStoreFilterFactory::createFilterFactoryFromProtoTyped(
    const envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto config = std::make_shared<TestKeyValueStoreFilterConfig>(proto_config);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TestKeyValueStoreFilter>(config));
  };
}

/**
 * Static registration for the TestKeyValueStore filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(TestKeyValueStoreFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TestKeyValueStore
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
