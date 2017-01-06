#include "mocks.h"

using testing::ReturnRef;

namespace LocalInfo {

MockLocalInfo::MockLocalInfo() {
  ON_CALL(*this, address()).WillByDefault(ReturnRef(address_));
  ON_CALL(*this, zoneName()).WillByDefault(ReturnRef(zone_name_));
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, nodeName()).WillByDefault(ReturnRef(node_name_));
}

MockLocalInfo::~MockLocalInfo() {}

} // LocalInfo
