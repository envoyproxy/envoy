#include "od_cds_api_handle.h"

#include "cluster_discovery_callback_handle.h"

namespace Envoy {
namespace Upstream {

using ::testing::Invoke;
using ::testing::NiceMock;

MockOdCdsApiHandle::MockOdCdsApiHandle() {
  ON_CALL(*this, requestOnDemandClusterDiscovery(_, _))
      .WillByDefault(
          Invoke([](const std::string&,
                    ClusterDiscoveryCallbackWeakPtr) -> ClusterDiscoveryCallbackHandlePtr {
            return std::make_unique<NiceMock<MockClusterDiscoveryCallbackHandle>>();
          }));
}

MockOdCdsApiHandle::~MockOdCdsApiHandle() = default;

} // namespace Upstream
} // namespace Envoy
