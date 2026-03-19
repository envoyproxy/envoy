#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

class FactoryTest : public testing::Test {
protected:
  bool isMutexLocked(Factory& factory) {
    auto locked = factory.mutex_.tryLock();
    if (locked) {
      factory.mutex_.unlock();
    }
    return !locked;
  }

  Thread::ThreadSynchronizer& synchronizer(Factory& factory) { return factory.synchronizer_; };

  Event::SimulatedTimeSystem time_system_;
  std::chrono::milliseconds time_to_next_token_;
  std::shared_ptr<Factory> factory_ = Factory::create(1000, time_system_);
  class AutoCancelRequest {
  public:
    AutoCancelRequest(std::shared_ptr<Request> request) : request_(std::move(request)) {}
    ~AutoCancelRequest() {
      if (request_) {
        request_->cancel();
      }
    }
    uint64_t consume(uint64_t want) { return request_->consume(want, true); }
    std::shared_ptr<Request> request_;
  };
  AutoCancelRequest makeRequest(absl::string_view tenant_name, uint64_t weight) {
    return AutoCancelRequest(factory_->getTenant(tenant_name, weight)->makeRequest());
  }
};

TEST_F(FactoryTest, NotImplementedConsumeWithTimeToNextToken) {
  auto request = makeRequest("foo", 1);
  std::chrono::milliseconds unused_out;
  EXPECT_ENVOY_BUG(request.request_->consume(750, true, unused_out), "not implemented");
}

TEST_F(FactoryTest, NotImplementedConsumeWithAllowPartialFalse) {
  auto request = makeRequest("foo", 1);
  EXPECT_ENVOY_BUG(request.request_->consume(750, false), "not implemented");
}

TEST_F(FactoryTest, InitializationAndTimeBothFillTheBucket) {
  auto request = makeRequest("foo", 1);
  EXPECT_EQ(750, request.consume(750));
  EXPECT_EQ(250, request.consume(750));
  EXPECT_EQ(0, request.consume(500));
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  EXPECT_EQ(500, request.consume(500));
}

TEST_F(FactoryTest, CompetingRequestsGetAppropriateShares) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 2);
  auto request2 = makeRequest("foo", 2);
  auto request3 = makeRequest("bar", 8);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, request0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, request1.consume(1000));
  EXPECT_EQ(0, request2.consume(1000));
  EXPECT_EQ(0, request3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Now everyone should get a split; the total weight is 10, 2 to foo and
  // 8 to bar, so foo should have 200 split between 2 requests, resulting in
  // 100 each, and bar should get 800 not split.
  EXPECT_EQ(100, request1.consume(1000));
  EXPECT_EQ(100, request2.consume(1000));
  EXPECT_EQ(800, request3.consume(1000));
}

TEST_F(FactoryTest, DeletingARequestCancelsItsShare) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 2);
  auto request2 = makeRequest("foo", 2);
  auto request3 = makeRequest("bar", 8);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, request0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, request1.consume(1000));
  EXPECT_EQ(0, request2.consume(1000));
  EXPECT_EQ(0, request3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Cancel bar's request by deletion.
  request3.request_->cancel();
  request3.request_.reset();
  // Now the other two should get a 50:50 split on the same tenant.
  EXPECT_EQ(500, request1.consume(1000));
  EXPECT_EQ(500, request2.consume(1000));
}

TEST_F(FactoryTest, PoorlyDividedSplitsStillEmptyTheBucket) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 1);
  auto request2 = makeRequest("bar", 73);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, request0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, request1.consume(1000));
  EXPECT_EQ(0, request2.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));

  // Each of them should get their expected amount maybe plus the indivisible
  // part. There is no hard rule about who gets the remainder.
  uint64_t remainder = 1000 % 74;
  uint64_t foo_fraction = 1000 / 74;
  uint64_t bar_fraction = 1000 / 74 * 73;
  EXPECT_EQ(foo_fraction + remainder, request1.consume(1000));
  EXPECT_EQ(bar_fraction, request2.consume(1000));
}

TEST_F(FactoryTest, PoorlyDividedSplitsCanBeSubdivided) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 1);
  auto request2 = makeRequest("foo2", 1);
  auto request3 = makeRequest("bar", 72);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, request0.consume(1000));
  // Queue a 1000 request for everyone else, except foo only wants to consume
  // a little bit of "indivisable spill".
  EXPECT_EQ(0, request1.consume(1000 / 74 + 2));
  EXPECT_EQ(0, request2.consume(1000));
  EXPECT_EQ(0, request3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Each should have between their share and their share plus the remainder,
  // which gets given to whoever is earlier in the queue.
  // Since foo only wants 2, it takes only part of the remainder, with the
  // rest of the remainder going to foo2.
  uint64_t remainder = 1000 % 74;
  uint64_t foo_fraction = 1000 / 74;
  uint64_t bar_fraction = 1000 / 74 * 72;
  EXPECT_EQ(foo_fraction + 2, request1.consume(1000 / 74 + 2));
  EXPECT_EQ(foo_fraction + remainder - 2, request2.consume(1000));
  EXPECT_EQ(bar_fraction, request3.consume(1000));
}

TEST_F(FactoryTest, UnclaimedSharesCanStackUp) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 1);
  // Empty the bucket and queue up another 4000 for baz.
  EXPECT_EQ(1000, request0.consume(5000));
  // Put foo in the queue too.
  EXPECT_EQ(0, request1.consume(1));
  // Fill the bucket and take one token for foo (baz will get 999).
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  EXPECT_EQ(1, request1.consume(1));
  // Fill the bucket and put foo back in the queue (baz will get 1000).
  time_system_.setMonotonicTime(std::chrono::seconds(2));
  EXPECT_EQ(0, request1.consume(1));
  // Fill the bucket and take one token for foo (baz will get 999).
  time_system_.setMonotonicTime(std::chrono::seconds(3));
  EXPECT_EQ(1, request1.consume(1));
  // Baz finally gets around to requesting the tokens it didn't already
  // get, and has saved up 2998 of them.
  EXPECT_EQ(2998, request0.consume(4000));
}

TEST_F(FactoryTest, RequestForLessThanEarmarkedFractionFreesUpTokensForOtherRequestsOrTenants) {
  auto request0 = makeRequest("baz", 1);
  auto request1 = makeRequest("foo", 1);
  auto request2 = makeRequest("foo", 1);
  auto request3 = makeRequest("bar", 1);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, request0.consume(1000));
  // Queue 100 for foo request 1, and 300 for foo request 2 - they will be splitting
  // 500, and should both be satisfied even though the fair share is only 250,
  // because there is enough left over from foo1 not claiming its entire share
  // for foo2 to get more.
  // Queue 1000 for bar - this should get 600 even though bar's share of 1000
  // is only 500, because foo only took 400 so bar can take the excess.
  EXPECT_EQ(0, request1.consume(100));
  EXPECT_EQ(0, request2.consume(300));
  EXPECT_EQ(0, request3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  uint64_t foo_gets = request1.consume(100);
  uint64_t foo2_gets = request2.consume(300);
  uint64_t bar_gets = request3.consume(1000);
  // Combined they should get all the tokens.
  EXPECT_EQ(1000, foo_gets + foo2_gets + bar_gets);
  // Both foos should be fully satisfied, and bar should have 600.
  EXPECT_EQ(100, foo_gets);
  EXPECT_EQ(300, foo2_gets);
  EXPECT_EQ(600, bar_gets);
}

} // namespace FairTokenBucket
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
