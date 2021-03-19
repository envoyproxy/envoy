#pragma once

#include <string>

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockOdCdsApi;
using MockOdCdsApiPtr = std::unique_ptr<MockOdCdsApi>;
using MockOdCdsApiSharedPtr = std::shared_ptr<MockOdCdsApi>;

class MockOdCdsApi : public OdCdsApi {
public:
  static MockOdCdsApiPtr create() { return std::make_unique<MockOdCdsApi>(); }

  static MockOdCdsApiSharedPtr createShared() { return create(); }

  MockOdCdsApi();
  ~MockOdCdsApi() override;

  MOCK_METHOD(void, updateOnDemand, (const std::string& cluster_name));
};

} // namespace Upstream
} // namespace Envoy
