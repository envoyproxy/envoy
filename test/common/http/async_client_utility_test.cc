#include "common/http/async_client_utility.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {

class AsyncClientRequestTrackerTest : public testing::Test {
public:
  std::unique_ptr<AsyncClientRequestTracker> active_requests_{
      std::make_unique<AsyncClientRequestTracker>()};

  NiceMock<MockAsyncClient> async_client_;
  StrictMock<MockAsyncClientRequest> request1_{&async_client_};
  StrictMock<MockAsyncClientRequest> request2_{&async_client_};
  StrictMock<MockAsyncClientRequest> request3_{&async_client_};
};

TEST_F(AsyncClientRequestTrackerTest, ShouldSupportRemoveWithoutAdd) {
  // Should not fail.
  active_requests_->remove(request1_);
}

TEST_F(AsyncClientRequestTrackerTest, OnDestructDoNothingIfThereAreNoActiveRequests) {
  // Trigger destruction.
  active_requests_.reset();
}

TEST_F(AsyncClientRequestTrackerTest, OnDestructCancelActiveRequests) {
  // Include active requests.
  active_requests_->add(request1_);
  active_requests_->add(request2_);
  active_requests_->add(request3_);
  // Exclude active requests.
  active_requests_->remove(request2_);

  // Must cancel active requests on destruction.
  EXPECT_CALL(request1_, cancel());
  EXPECT_CALL(request3_, cancel());

  // Trigger destruction.
  active_requests_.reset();
}

} // namespace
} // namespace Http
} // namespace Envoy
