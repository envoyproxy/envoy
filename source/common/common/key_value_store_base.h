#pragma once

#include "envoy/common/key_value_store.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {

// This is the base implementation of the KeyValueStore. It handles the various
// functions other than flush(), which will be implemented by subclasses.
//
// Note this implementation HAS UNBOUNDED SIZE
// It is assumed the callers manage the number of entries. Use with care.
class KeyValueStoreBase : public KeyValueStore {
public:
  // Sets up flush() for the configured interval.
  KeyValueStoreBase(Event::Dispatcher& dispatcher, std::chrono::seconds flush_interval);

  // From KeyValueStore
  void addOrUpdateKey(absl::string_view key, absl::string_view value) override;
  void removeKey(absl::string_view key) override;
  absl::string_view getKey(absl::string_view key) override;

protected:
  const Event::TimerPtr flush_timer_;
  absl::flat_hash_map<std::string, std::string> store_;
};

// A filesystem based key value store, which loads from and flushes to the file
// provided.
//
// All keys and values are flushed to a single file as
// [length]\n[key][length]\n[value]
class FileBasedKeyValueStore : public KeyValueStoreBase {
public:
  FileBasedKeyValueStore(Event::Dispatcher& dispatcher, std::chrono::seconds flush_interval,
                         Filesystem::Instance& file_system, const std::string filename);
  // From KeyValueStore
  void flush() override;

private:
  Filesystem::Instance& file_system_;
  const std::string filename_;
};

} // namespace Envoy
