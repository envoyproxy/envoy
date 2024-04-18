#include "library/cc/key_value_store.h"

#include "library/common/data/utility.h"

namespace Envoy {
namespace Platform {

namespace {

envoy_data c_kv_store_read(envoy_data key, const void* context) {
  auto kv_store = *static_cast<KeyValueStoreSharedPtr*>(const_cast<void*>(context));

  auto value = kv_store->read(Data::Utility::copyToString(key));
  release_envoy_data(key);

  return value.has_value() ? Data::Utility::copyToBridgeData(value.value()) : envoy_nodata;
}

void c_kv_store_save(envoy_data key, envoy_data value, const void* context) {
  auto kv_store = *static_cast<KeyValueStoreSharedPtr*>(const_cast<void*>(context));

  kv_store->save(Data::Utility::copyToString(key), Data::Utility::copyToString(value));
  release_envoy_data(key);
  release_envoy_data(value);
}

void c_kv_store_remove(envoy_data key, const void* context) {
  auto kv_store = *static_cast<KeyValueStoreSharedPtr*>(const_cast<void*>(context));

  kv_store->remove(Data::Utility::copyToString(key));
  release_envoy_data(key);
}

} // namespace

envoy_kv_store KeyValueStore::asEnvoyKeyValueStore() {
  return envoy_kv_store{
      &c_kv_store_read,
      &c_kv_store_save,
      &c_kv_store_remove,
      new KeyValueStoreSharedPtr(shared_from_this()),
  };
}

} // namespace Platform
} // namespace Envoy
