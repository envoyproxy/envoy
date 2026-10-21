#pragma once

#include <cstdint>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "test/mocks/stats/mocks.h"

#include "absl/container/node_hash_map.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

class MockSnapshot : public Snapshot {
public:
  MockSnapshot();
  ~MockSnapshot() override;

  // Provide a default implementation of mocked featureEnabled/2.
  bool featureEnabledDefault(absl::string_view, uint64_t default_value) {
    if (default_value == 0) {
      return false;
    } else if (default_value == 100) {
      return true;
    } else {
      PANIC("Not implemented yet. You may want to set expectation of mocked"
            "featureEnabled() instead.");
    }
  }

  MOCK_METHOD(bool, deprecatedFeatureEnabled, (absl::string_view key, bool default_enabled),
              (const, override));
  MOCK_METHOD(bool, runtimeFeatureEnabled, (absl::string_view key), (const, override));
  MOCK_METHOD(bool, featureEnabled, (absl::string_view key, uint64_t default_value),
              (const, override));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, uint64_t default_value, uint64_t random_value),
              (const, override));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, uint64_t default_value, uint64_t random_value,
               uint64_t num_buckets),
              (const, override));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, const envoy::type::v3::FractionalPercent& default_value),
              (const, override));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, const envoy::type::v3::FractionalPercent& default_value,
               uint64_t random_value),
              (const, override));
  MOCK_METHOD(ConstStringOptRef, get, (absl::string_view key), (const, override));
  MOCK_METHOD(uint64_t, getInteger, (absl::string_view key, uint64_t default_value),
              (const, override));
  MOCK_METHOD(double, getDouble, (absl::string_view key, double default_value), (const, override));
  MOCK_METHOD(bool, getBoolean, (absl::string_view key, bool default_value), (const, override));
  MOCK_METHOD(const std::vector<OverrideLayerConstPtr>&, getLayers, (), (const, override));
};

class MockLoader : public Loader {
public:
  MockLoader();
  ~MockLoader() override;

  MOCK_METHOD(absl::Status, initialize, (Upstream::ClusterManager & cm), (override));
  MOCK_METHOD(absl::Status, onWorkerThreadsRegistered, (), (override));
  MOCK_METHOD(const Snapshot&, snapshot, (), (const, override));
  MOCK_METHOD(SnapshotConstSharedPtr, threadsafeSnapshot, (), (override));
  MOCK_METHOD(absl::Status, mergeValues, ((const absl::node_hash_map<std::string, std::string>&)),
              (override));
  MOCK_METHOD(void, startRtdsSubscriptions, (ReadyCallback), (override));
  MOCK_METHOD(Stats::Scope&, getRootScope, (), (override));
  MOCK_METHOD(void, countDeprecatedFeatureUse, (), (const, override));

  SnapshotConstSharedPtr threadsafe_snapshot_{
      std::make_shared<const testing::NiceMock<MockSnapshot>>()};
  testing::NiceMock<MockSnapshot> snapshot_;
  testing::NiceMock<Stats::MockStore> store_;
};

class MockOverrideLayer : public Snapshot::OverrideLayer {
public:
  MockOverrideLayer();
  ~MockOverrideLayer() override;

  MOCK_METHOD(const std::string&, name, (), (const, override));
  MOCK_METHOD(const Snapshot::EntryMap&, values, (), (const, override));
};

} // namespace Runtime
} // namespace Envoy
