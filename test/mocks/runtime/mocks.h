#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

class MockRandomGenerator : public RandomGenerator {
public:
  MockRandomGenerator();
  ~MockRandomGenerator() override;

  MOCK_METHOD0(random, uint64_t());
  MOCK_METHOD0(uuid, std::string());

  const std::string uuid_{"a121e9e1-feae-4136-9e0e-6fac343d56c9"};
};

class MockSnapshot : public Snapshot {
public:
  MockSnapshot();
  ~MockSnapshot() override;

  // Provide a default implementation of mocked featureEnabled/2.
  bool featureEnabledDefault(const std::string&, uint64_t default_value) {
    if (default_value == 0) {
      return false;
    } else if (default_value == 100) {
      return true;
    } else {
      throw std::invalid_argument("Not implemented yet. You may want to set expectation of mocked "
                                  "featureEnabled() instead.");
    }
  }

  MOCK_CONST_METHOD1(deprecatedFeatureEnabled, bool(const std::string& key));
  MOCK_CONST_METHOD1(runtimeFeatureEnabled, bool(absl::string_view key));
  MOCK_CONST_METHOD2(featureEnabled, bool(const std::string& key, uint64_t default_value));
  MOCK_CONST_METHOD3(featureEnabled,
                     bool(const std::string& key, uint64_t default_value, uint64_t random_value));
  MOCK_CONST_METHOD4(featureEnabled, bool(const std::string& key, uint64_t default_value,
                                          uint64_t random_value, uint64_t num_buckets));
  MOCK_CONST_METHOD2(featureEnabled, bool(const std::string& key,
                                          const envoy::type::FractionalPercent& default_value));
  MOCK_CONST_METHOD3(featureEnabled, bool(const std::string& key,
                                          const envoy::type::FractionalPercent& default_value,
                                          uint64_t random_value));
  MOCK_CONST_METHOD1(get, const std::string&(const std::string& key));
  MOCK_CONST_METHOD2(getInteger, uint64_t(const std::string& key, uint64_t default_value));
  MOCK_CONST_METHOD2(getDouble, double(const std::string& key, double default_value));
  MOCK_CONST_METHOD2(getBoolean, bool(absl::string_view key, bool default_value));
  MOCK_CONST_METHOD0(getLayers, const std::vector<OverrideLayerConstPtr>&());
};

class MockLoader : public Loader {
public:
  MockLoader();
  ~MockLoader() override;

  MOCK_METHOD1(initialize, void(Upstream::ClusterManager& cm));
  MOCK_METHOD0(snapshot, const Snapshot&());
  MOCK_METHOD0(threadsafeSnapshot, std::shared_ptr<const Snapshot>());
  MOCK_METHOD1(mergeValues, void(const std::unordered_map<std::string, std::string>&));

  testing::NiceMock<MockSnapshot> snapshot_;
};

class MockOverrideLayer : public Snapshot::OverrideLayer {
public:
  MockOverrideLayer();
  ~MockOverrideLayer() override;

  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(values, const Snapshot::EntryMap&());
};

} // namespace Runtime
} // namespace Envoy
