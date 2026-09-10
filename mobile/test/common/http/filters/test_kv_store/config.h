#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_kv_store/filter.h"
#include "test/common/http/filters/test_kv_store/filter.pb.h"
#include "test/common/http/filters/test_kv_store/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestKeyValueStore {

/**
 * Config registration for the TestKeyValueStore filter. @see NamedHttpFilterConfigFactory.
 */
class TestKeyValueStoreFilterFactory
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore> {
public:
  TestKeyValueStoreFilterFactory() : UnifiedFactoryBase("test_kv_store") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(TestKeyValueStoreFilterFactory);

} // namespace TestKeyValueStore
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
