#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"
#include "library/common/extensions/key_value/platform/c_types.h"

namespace Envoy {
namespace Platform {

/**
 * `KeyValueStore` is an interface that may be implemented to provide access to an arbitrary
 * key-value store implementation that may be made accessible to internal Envoy Mobile code.
 */
struct KeyValueStore : public std::enable_shared_from_this<KeyValueStore> {
public:
  virtual ~KeyValueStore() = default;

  /**
   * Returns the value associated with the provided key, if any.
   * @param key supplies a key to return the value of.
   * @return the value, if the key is in the store, absl::nullopt otherwise.
   */
  virtual absl::optional<std::string> read(const std::string& key) PURE;

  /**
   * Adds or updates a key:value pair in the store.
   * @param key supplies a key to add or update.
   * @param value supplies the value to set for that key.
   */
  virtual void save(std::string key, std::string value) PURE;

  /**
   * Removes a key:value pair from the store. This is a no-op if the key is not present.
   * @param key supplies a key to remove from the store.
   */
  virtual void remove(const std::string& key) PURE;

  /**
   * Maps an implementation to its internal representation.
   * @return portable internal type used to call an implementation.
   */
  virtual envoy_kv_store asEnvoyKeyValueStore() final;
};

using KeyValueStoreSharedPtr = std::shared_ptr<KeyValueStore>;

} // namespace Platform
} // namespace Envoy
