#pragma once

#include "source/common/upstream/od_cds_api_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class MockMissingClusterNotifier : public MissingClusterNotifier {
public:
  MockMissingClusterNotifier();
  ~MockMissingClusterNotifier() override;

  MOCK_METHOD(void, notifyMissingCluster, (absl::string_view name));
};

} // namespace Upstream
} // namespace Envoy
