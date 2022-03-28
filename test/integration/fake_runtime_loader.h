#pragma once

#include "envoy/runtime/runtime.h"

namespace Envoy {

/**
 * A fake Runtime Loader that implements some of the functionality of
 * the runtime loader. This is only meant to be used by FakeUpstream,
 * and doesn't share any runtime feature values with Envoy's runtime
 * (see source/common/runtime/runtime_features.h).
 */
class FakeRuntimeLoader : public Runtime::Loader {
public:
  FakeRuntimeLoader() = default;
  ~FakeRuntimeLoader() override = default;

  void initialize(Upstream::ClusterManager&) override { snapshot_.clear(); }

  const Runtime::Snapshot& snapshot() override { return snapshot_; }

  Runtime::SnapshotConstSharedPtr threadsafeSnapshot() override {
    return std::make_shared<const FakeSnapshot>(snapshot_);
  }

  void mergeValues(const absl::node_hash_map<std::string, std::string>&) override {
    PANIC("unimplemented");
  }

  void startRtdsSubscriptions(ReadyCallback) override { PANIC("unimplemented"); }

  Stats::Scope& getRootScope() override { PANIC("not implemented"); }

  void countDeprecatedFeatureUse() const override {}

  class FakeSnapshot : public Runtime::Snapshot {
  public:
    FakeSnapshot() = default;
    FakeSnapshot(const FakeSnapshot& other) { entry_map_ = other.entry_map_; }
    ~FakeSnapshot() = default;

    bool deprecatedFeatureEnabled(absl::string_view key, bool default_enabled) const override {
      return getBoolean(key, default_enabled);
    }

    bool runtimeFeatureEnabled(absl::string_view key) const override {
      return getBoolean(key, false);
    }

    bool featureEnabled(absl::string_view key, uint64_t default_value) const override {
      return getBoolean(key, default_value);
    }

    bool featureEnabled(absl::string_view key, uint64_t default_value,
                        uint64_t random_value) const override {
      return featureEnabled(key, default_value, random_value, 100);
    }

    bool featureEnabled(absl::string_view key, uint64_t default_value, uint64_t random_value,
                        uint64_t num_buckets) const override {
      return random_value % num_buckets < std::min(getInteger(key, default_value), num_buckets);
    }

    bool featureEnabled(absl::string_view,
                        const envoy::type::v3::FractionalPercent&) const override {
      PANIC("not implemented");
    };

    bool featureEnabled(absl::string_view, const envoy::type::v3::FractionalPercent&,
                        uint64_t) const override {
      PANIC("not implemented");
    }

    Runtime::Snapshot::ConstStringOptRef get(absl::string_view key) const override {
      if (auto map_it = entry_map_.find(key); map_it != entry_map_.end()) {
        return map_it->second.raw_string_value_;
      }
      return absl::nullopt;
    }

    uint64_t getInteger(absl::string_view key, uint64_t default_value) const override {
      if (auto map_it = entry_map_.find(key); map_it != entry_map_.end()) {
        auto value = map_it->second.uint_value_;
        if (value.has_value()) {
          return value.value();
        }
      }
      return default_value;
    }

    double getDouble(absl::string_view key, double default_value) const override {
      if (auto map_it = entry_map_.find(key); map_it != entry_map_.end()) {
        auto value = map_it->second.double_value_;
        if (value.has_value()) {
          return value.value();
        }
      }
      return default_value;
    }

    bool getBoolean(absl::string_view key, bool default_value) const override {
      if (auto map_it = entry_map_.find(key); map_it != entry_map_.end()) {
        auto value = map_it->second.bool_value_;
        if (value.has_value()) {
          return value.value();
        }
      }
      return default_value;
    }

    const std::vector<Runtime::Snapshot::OverrideLayerConstPtr>& getLayers() const override {
      // TODO - Panic
      return empty_layers_;
    }

    // Clears the current snapshot.
    void clear() { entry_map_.clear(); }

  private:
    Runtime::Snapshot::EntryMap entry_map_;
    std::vector<Runtime::Snapshot::OverrideLayerConstPtr> empty_layers_;
  };

private:
  FakeSnapshot snapshot_;
};

} // namespace Envoy
