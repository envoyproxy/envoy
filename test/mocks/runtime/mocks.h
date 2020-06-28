#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

class MockRandomGenerator : public RandomGenerator {
public:
  MockRandomGenerator();
  ~MockRandomGenerator() override;

  MOCK_METHOD(uint64_t, random, ());
  MOCK_METHOD(std::string, uuid, ());

  const std::string uuid_{"a121e9e1-feae-4136-9e0e-6fac343d56c9"};
};

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
      throw std::invalid_argument("Not implemented yet. You may want to set expectation of mocked "
                                  "featureEnabled() instead.");
    }
  }

  MOCK_METHOD(void, countDeprecatedFeatureUse, (), (const));
  MOCK_METHOD(bool, deprecatedFeatureEnabled, (absl::string_view key, bool default_enabled),
              (const));
  MOCK_METHOD(bool, runtimeFeatureEnabled, (absl::string_view key), (const));
  MOCK_METHOD(bool, featureEnabled, (absl::string_view key, uint64_t default_value), (const));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, uint64_t default_value, uint64_t random_value), (const));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, uint64_t default_value, uint64_t random_value,
               uint64_t num_buckets),
              (const));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, const envoy::type::v3::FractionalPercent& default_value),
              (const));
  MOCK_METHOD(bool, featureEnabled,
              (absl::string_view key, const envoy::type::v3::FractionalPercent& default_value,
               uint64_t random_value),
              (const));
  MOCK_METHOD(ConstStringOptRef, get, (absl::string_view key), (const));
  MOCK_METHOD(uint64_t, getInteger, (absl::string_view key, uint64_t default_value), (const));
  MOCK_METHOD(double, getDouble, (absl::string_view key, double default_value), (const));
  MOCK_METHOD(bool, getBoolean, (absl::string_view key, bool default_value), (const));
  MOCK_METHOD(const std::vector<OverrideLayerConstPtr>&, getLayers, (), (const));
};

class MockLoader : public Loader {
public:
  MockLoader();
  ~MockLoader() override;

  MOCK_METHOD(void, initialize, (Upstream::ClusterManager & cm));
  MOCK_METHOD(const Snapshot&, snapshot, ());
  MOCK_METHOD(SnapshotConstSharedPtr, threadsafeSnapshot, ());
  MOCK_METHOD(void, mergeValues, ((const std::unordered_map<std::string, std::string>&)));
  MOCK_METHOD(void, startRtdsSubscriptions, (ReadyCallback));
  MOCK_METHOD(Stats::Scope&, getRootScope, ());

  testing::NiceMock<MockSnapshot> snapshot_;
  testing::NiceMock<Stats::MockStore> store_;
};

class MockOverrideLayer : public Snapshot::OverrideLayer {
public:
  MockOverrideLayer();
  ~MockOverrideLayer() override;

  MOCK_METHOD(const std::string&, name, (), (const));
  MOCK_METHOD(const Snapshot::EntryMap&, values, (), (const));
};

} // namespace Runtime
} // namespace Envoy
