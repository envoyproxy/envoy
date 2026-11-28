#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/subscription.h"

#include "source/common/config/decoded_resource_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

/**
 * Remembers the decoded form of each resource from the previous update, keyed by its serialized
 * bytes. A SotW update re-sends every resource; an unchanged one is then not unpacked and
 * validated again. Resources absent from an update are forgotten at endUpdate(), so the cache
 * holds at most the current configuration.
 *
 * Not thread safe: one cache per subscription, used on the thread that decodes its updates.
 */
class DecodedResourceCache {
public:
  /**
   * Decodes `resource`, or reuses the previous decode of identical bytes. `version` is the
   * update's version, used unless the resource carries its own (a Resource wrapper).
   */
  absl::StatusOr<DecodedResourcePtr> decode(OpaqueResourceDecoder& resource_decoder,
                                            const Protobuf::Any& resource,
                                            const std::string& version, Protobuf::Arena& arena);

  /**
   * Ends an update: resources not seen in it are dropped. Call after a successful decode of
   * every resource of the update.
   */
  void endUpdate();

  /**
   * Drops a partly decoded update (for example after a decode error). Entries it had taken over
   * from the previous update go back, so that update stays the reference.
   */
  void abortUpdate() {
    previous_.merge(current_);
    current_.clear();
  }

  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }

private:
  struct Entry {
    std::unique_ptr<Protobuf::Message> message;
    std::string name;
    std::vector<std::string> aliases;
    // Set when the resource carried its own version (a Resource wrapper).
    std::optional<std::string> own_version;
    std::optional<std::chrono::milliseconds> ttl;
    std::optional<envoy::config::core::v3::Metadata> metadata;
  };
  using EntrySharedPtr = std::shared_ptr<const Entry>;
  using Map = absl::flat_hash_map<std::string, EntrySharedPtr>;

  // A reused decode with the version of the update it is delivered in.
  class CachedResource : public DecodedResource {
  public:
    CachedResource(EntrySharedPtr entry, const std::string& version)
        : entry_(std::move(entry)), version_(entry_->own_version.value_or(version)) {}
    const std::string& name() const override { return entry_->name; }
    const std::vector<std::string>& aliases() const override { return entry_->aliases; }
    const std::string& version() const override { return version_; }
    const Protobuf::Message& resource() const override { return *entry_->message; }
    std::optional<std::chrono::milliseconds> ttl() const override { return entry_->ttl; }
    bool hasResource() const override { return true; }
    const OptRef<const envoy::config::core::v3::Metadata> metadata() const override {
      return entry_->metadata.has_value() ? makeOptRef(entry_->metadata.value()) : std::nullopt;
    }
    bool fromCache() const override { return false; }

  private:
    const EntrySharedPtr entry_;
    const std::string version_;
  };

  static std::string keyOf(const Protobuf::Any& resource);

  Map previous_;
  Map current_;
  uint64_t hits_{};
  uint64_t misses_{};
};

} // namespace Config
} // namespace Envoy
