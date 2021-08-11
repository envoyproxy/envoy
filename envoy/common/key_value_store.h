#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

// A key value store, designed to periodically flush key value pairs to long
// term storage (disk or otherwise)
class KeyValueStore {
public:
  virtual ~KeyValueStore() = default;

  /**
   * Adds or updates a key:value pair in the store.
   * @param key supplies a key to add or update.
   * @param value supplies the value to set for that key.
   */
  virtual void addOrUpdate(absl::string_view key, absl::string_view value) PURE;

  /**
   * Removes a key:value pair from the store. This is a no-op if the key is not present.
   * @param key supplies a key to remove from the store.
   */
  virtual void remove(absl::string_view key) PURE;

  /**
   * Returns the value of the key provided.
   * @param key supplies a key to return the value of.
   * @return the value, if the key is in the store, absl::nullopt otherwise.
   */
  virtual absl::optional<absl::string_view> get(absl::string_view key) PURE;

  /**
   * Flushes the store to long term storage.
   */
  virtual void flush() PURE;
};

} // namespace Envoy
