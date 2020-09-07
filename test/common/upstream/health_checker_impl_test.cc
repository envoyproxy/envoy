#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/health_check.pb.validate.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/transport_socket_match.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {


static void addUint8(Buffer::Instance& buffer, uint8_t addend) {
  buffer.add(&addend, sizeof(addend));
}

// Tests an unsuccessful healthcheck, where the endpoint sends wrong data
TEST_F(TcpHealthCheckerImplTest, WrongData) {
  InSequence s;

  setupDataDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(1);
  EXPECT_CALL(*timeout_timer_, enableTimer(_, _));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Not the expected response
  Buffer::OwnedImpl response;
  addUint8(response, 3);
  read_filter_->onData(response, false);

  // These are the expected metric results after testing.
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  // TODO(lilika): This should indicate a failure
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->getActiveHealthFailureType(),
            Host::ActiveHealthFailureType::UNHEALTHY);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
