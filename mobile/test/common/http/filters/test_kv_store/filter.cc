#include "test/common/http/filters/test_kv_store/filter.h"

#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"

#include "library/common/bridge/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestKeyValueStore {

TestKeyValueStoreFilterConfig::TestKeyValueStoreFilterConfig(
    const envoymobile::extensions::filters::http::test_kv_store::TestKeyValueStore& proto_config)
    : kv_store_(
          static_cast<envoy_kv_store*>(Api::External::retrieveApi(proto_config.kv_store_name()))) {}

Http::FilterHeadersStatus TestKeyValueStoreFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto store = config_->keyValueStore();
  auto key = Bridge::Utility::copyToBridgeData(config_->testKey());
  RELEASE_ASSERT(Bridge::Utility::copyToString(store->read(key, store->context)).empty(),
                 "store should be empty");

  envoy_data value = Bridge::Utility::copyToBridgeData(config_->testValue());
  store->save(key, value, store->context);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus TestKeyValueStoreFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  const auto store = config_->keyValueStore();
  auto key = Bridge::Utility::copyToBridgeData(config_->testKey());
  RELEASE_ASSERT(Bridge::Utility::copyToString(store->read(key, store->context)) ==
                     config_->testValue(),
                 "store did not contain expected value");

  store->remove(key, store->context);
  RELEASE_ASSERT(Bridge::Utility::copyToString(store->read(key, store->context)).empty(),
                 "store should be empty");

  return Http::FilterHeadersStatus::Continue;
}
} // namespace TestKeyValueStore
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
