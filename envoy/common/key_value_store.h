#pragma once

#include <chrono>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/protobuf/message_validator.h"

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
   * @param ttl optionally specifies a lifetime after which this entry will be removed.
   * ttl must be greater than 0.
   */
  virtual void addOrUpdate(absl::string_view key, absl::string_view value,
                           absl::optional<std::chrono::seconds> ttl) PURE;

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

  // Returns for the iterate function.
  enum class Iterate { Continue, Break };

  /**
   * Callback when calling iterate() in a key value store.
   * @param key is the key for a given entry
   * @param value is the value for a given entry
   * @return Iterate::Continue to continue iteration, or Iterate::Break to stop.
   */
  using ConstIterateCb = std::function<Iterate(const std::string& key, const std::string& value)>;

  /**
   * Iterate over a key value store.
   * @param cb supplies the iteration callback.
   */
  virtual void iterate(ConstIterateCb cb) const PURE;
};

using KeyValueStorePtr = std::unique_ptr<KeyValueStore>;

// A factory for creating key value stores.
class KeyValueStoreFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Function to create KeyValueStores from the specified config.
   * @param cb supplies the key value store configuration
   * @param validation_visitor the configuration validator
   * @dispatcher the dispatcher for the thread, for flush alarms.
   * @file_system the file system.
   * @return a new key value store.
   */
  virtual KeyValueStorePtr createStore(const Protobuf::Message& config,
                                       ProtobufMessage::ValidationVisitor& validation_visitor,
                                       Event::Dispatcher& dispatcher,
                                       Filesystem::Instance& file_system) PURE;

  // @brief the category of the key value store for factory registration.
  std::string category() const override { return "envoy.common.key_value"; }
};

} // namespace Envoy
