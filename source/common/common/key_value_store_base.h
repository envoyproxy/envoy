#pragma once

#include <chrono>

#include "envoy/common/key_value_store.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "source/common/common/logger.h"
#include "source/common/config/ttl.h"

#include "quiche/common/quiche_linked_hash_map.h"

namespace Envoy {
inline constexpr absl::string_view KV_STORE_TTL_KEY = "TTL";

// This is the base implementation of the KeyValueStore. It handles the various
// functions other than flush(), which will be implemented by subclasses.
class KeyValueStoreBase : public KeyValueStore,
                          public Logger::Loggable<Logger::Id::key_value_store> {
public:
  // Sets up flush() for the configured interval.
  KeyValueStoreBase(Event::Dispatcher& dispatcher, std::chrono::milliseconds flush_interval,
                    uint32_t max_entries);

  // If |contents| is in the form of
  // [length]\n[key][length]\n[value]
  // parses key value pairs from |contents| and inserts into store_.
  // Returns true on success and false on failure.
  bool parseContents(absl::string_view contents);
  // Callback function for ttlManager.
  void onExpiredKeys(const std::vector<std::string>& keys);

  // KeyValueStore
  void addOrUpdate(absl::string_view key, absl::string_view value,
                   absl::optional<std::chrono::seconds> ttl) override;
  void remove(absl::string_view key) override;
  absl::optional<absl::string_view> get(absl::string_view key) override;
  void iterate(ConstIterateCb cb) const override;

protected:
  // Values in a KeyValueStore have an optional TTL.
  struct ValueWithTtl {
    ValueWithTtl(std::string value, absl::optional<std::chrono::seconds> ttl)
        : value_(value), ttl_(ttl) {}
    std::string value_;
    absl::optional<std::chrono::seconds> ttl_;
  };

  using KeyValueMap = quiche::QuicheLinkedHashMap<std::string, ValueWithTtl>;

  const KeyValueMap& store() { return store_; }

private:
  const uint32_t max_entries_;
  const Event::TimerPtr flush_timer_;
  Config::TtlManager ttl_manager_;
  KeyValueMap store_;
  // Used for validation only.
  mutable bool under_iterate_{};
  TimeSource& time_source_;
};

} // namespace Envoy
