#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/test_kv_store/filter.h"
#include "library/common/extensions/filters/http/test_kv_store/filter.pb.h"
#include "library/common/extensions/filters/http/test_kv_store/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestKeyValueStore {

/**
 * Config registration for the TestKeyValueStore filter. @see NamedHttpFilterConfigFactory.
 */
class TestKeyValueStoreFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore> {
public:
  TestKeyValueStoreFilterFactory() : FactoryBase("test_kv_store") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(TestKeyValueStoreFilterFactory);

} // namespace TestKeyValueStore
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
