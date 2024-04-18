#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockOdCdsApiHandle;
using MockOdCdsApiHandlePtr = std::unique_ptr<MockOdCdsApiHandle>;

class MockOdCdsApiHandle : public OdCdsApiHandle {
public:
  static MockOdCdsApiHandlePtr create() { return std::make_unique<MockOdCdsApiHandle>(); }
  MockOdCdsApiHandle();
  ~MockOdCdsApiHandle() override;

  MOCK_METHOD(ClusterDiscoveryCallbackHandlePtr, requestOnDemandClusterDiscovery,
              (absl::string_view name, ClusterDiscoveryCallbackPtr callback,
               std::chrono::milliseconds timeout));
};

} // namespace Upstream
} // namespace Envoy
