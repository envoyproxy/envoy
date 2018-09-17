#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "envoy/runtime/runtime.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

class MockRandomGenerator : public RandomGenerator {
public:
  MockRandomGenerator();
  ~MockRandomGenerator();

  MOCK_METHOD0(random, uint64_t());
  MOCK_METHOD0(uuid, std::string());

  const std::string uuid_{"a121e9e1-feae-4136-9e0e-6fac343d56c9"};
};

class MockSnapshot : public Snapshot {
public:
  MockSnapshot();
  ~MockSnapshot();

  MOCK_CONST_METHOD2(featureEnabled, bool(const std::string& key, uint64_t default_value));
  MOCK_CONST_METHOD3(featureEnabled,
                     bool(const std::string& key, uint64_t default_value, uint64_t random_value));
  MOCK_CONST_METHOD4(featureEnabled, bool(const std::string& key, uint64_t default_value,
                                          uint64_t random_value, uint64_t num_buckets));
  MOCK_CONST_METHOD1(get, const std::string&(const std::string& key));
  MOCK_CONST_METHOD2(getInteger, uint64_t(const std::string& key, uint64_t default_value));
  MOCK_CONST_METHOD0(getLayers, const std::vector<OverrideLayerConstPtr>&());
};

class MockLoader : public Loader {
public:
  MockLoader();
  ~MockLoader();

  MOCK_METHOD0(snapshot, Snapshot&());
  MOCK_METHOD1(mergeValues, void(const std::unordered_map<std::string, std::string>&));

  testing::NiceMock<MockSnapshot> snapshot_;
};

class MockOverrideLayer : public Snapshot::OverrideLayer {
public:
  MockOverrideLayer();
  ~MockOverrideLayer();

  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_CONST_METHOD0(values, const std::unordered_map<std::string, Snapshot::Entry>&());
};

} // namespace Runtime
} // namespace Envoy
