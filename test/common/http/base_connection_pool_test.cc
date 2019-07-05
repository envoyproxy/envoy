#include "common/http/conn_pool_base.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {

class TestConnPoolImplBase : public ConnPoolImplBase {
public:
  TestConnPoolImplBase()
      : ConnPoolImplBase(Upstream::makeTestHost(std::shared_ptr<Upstream::MockClusterInfo>(
                                                    new NiceMock<Upstream::MockClusterInfo>()),
                                                "tcp://127.0.0.1:9000"),
                         Upstream::ResourcePriority::Default) {}
  using ConnPoolImplBase::newPendingRequest;
  using ConnPoolImplBase::onPendingRequestCancel;
  using ConnPoolImplBase::purgePendingRequests;

  void checkForDrained() override {}
};

class Callbacks : public ConnectionPool::Callbacks {
public:
  void onPoolFailure(ConnectionPool::PoolFailureReason, absl::string_view,
                     Upstream::HostDescriptionConstSharedPtr) override {
    if (cancelable_ != nullptr) {
      cancelable_->cancel();
    }
  }

  void onPoolReady(Http::StreamEncoder&, Upstream::HostDescriptionConstSharedPtr) override {
    if (cancelable_ != nullptr) {
      cancelable_->cancel();
    }
  }

  ConnectionPool::Cancellable* cancelable_{};
};

TEST(ConnPoolImplBase, VerifyCancellationWhilePurging) {
  TestConnPoolImplBase cpb;
  NiceMock<Http::MockStreamDecoder> decoder_;
  std::string failure_reason = "failure";
  // simulate a request issued
  Callbacks handle1Callbacks;
  auto handle1 = cpb.newPendingRequest(decoder_, handle1Callbacks);

  // simulate another request issued, where the second request cancels the first request.
  Callbacks handle2Callbacks;
  // as a result of handle2 cancellation, handle1 is cancelled.
  handle2Callbacks.cancelable_ = handle1;
  /*  auto handle2 = */ cpb.newPendingRequest(decoder_, handle2Callbacks);

  // simulate the host is down, which purges all requests
  EXPECT_NO_THROW(cpb.purgePendingRequests(nullptr, absl::string_view(failure_reason)));
}

} // namespace Http
} // namespace Envoy
