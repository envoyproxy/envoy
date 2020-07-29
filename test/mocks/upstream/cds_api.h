#pragma once

#include <functional>
#include <string>

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockCdsApi : public CdsApi {
public:
  MockCdsApi();
  ~MockCdsApi() override;

  MOCK_METHOD(void, initialize, ());
  MOCK_METHOD(void, setInitializedCb, (std::function<void()> callback));
  MOCK_METHOD(const std::string, versionInfo, (), (const));

  std::function<void()> initialized_callback_;
};
} // namespace Upstream
} // namespace Envoy
