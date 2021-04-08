#pragma once

#include "envoy/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockOdCdsApiHandle : public OdCdsApiHandle {
public:
  MockOdCdsApiHandle();
  ~MockOdCdsApiHandle() override;

  MOCK_METHOD(ClusterDiscoveryCallbackHandlePtr, requestOnDemandClusterDiscovery,
              (const std::string& name, ClusterDiscoveryCallbackWeakPtr callback));
};

} // namespace Upstream
} // namespace Envoy
