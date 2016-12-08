#include "mocks.h"

using testing::ReturnRef;

namespace RateLimit {

MockClient::MockClient() {}
MockClient::~MockClient() {}

MockFilterConfig::MockFilterConfig() {
  ON_CALL(*this, domain()).WillByDefault(ReturnRef(domain_));
  ON_CALL(*this, localServiceCluster()).WillByDefault(ReturnRef(local_service_cluster_));
  ON_CALL(*this, stage()).WillByDefault(ReturnRef(stage_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(loader_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(store_));
}

MockFilterConfig::~MockFilterConfig() {}
} // RateLimit
