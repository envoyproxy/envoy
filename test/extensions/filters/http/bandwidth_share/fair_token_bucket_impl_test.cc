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
};

TEST_F(FactoryTest, NotImplementedConsumeWithTimeToNextToken) {
  Client client(*factory_, "foo", 1);
  std::chrono::milliseconds unused_out;
  EXPECT_ENVOY_BUG(client.consume(750, true, unused_out), "not expected");
}

TEST_F(FactoryTest, NotImplementedConsumeWithAllowPartialFalse) {
  Client client(*factory_, "foo", 1);
  EXPECT_ENVOY_BUG(client.consume(750, false), "not expected");
}

TEST_F(FactoryTest, NotImplementedNextTokenAvailable) {
  Client client(*factory_, "foo", 1);
  EXPECT_ENVOY_BUG(client.nextTokenAvailable(), "not expected");
}

TEST_F(FactoryTest, MaybeResetDoesNothingAndThisTestJustProvidesCoverage) {
  Client client(*factory_, "foo", 1);
  client.maybeReset(12345);
}

TEST_F(FactoryTest, InitializationAndTimeBothFillTheBucket) {
  Client client(*factory_, "foo", 1);
  EXPECT_EQ(750, client.consume(750));
  EXPECT_EQ(250, client.consume(750));
  EXPECT_EQ(0, client.consume(500));
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  EXPECT_EQ(500, client.consume(500));
}

TEST_F(FactoryTest, CompetingRequestsGetAppropriateShares) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 2);
  Client client2(*factory_, "foo", 2);
  Client client3(*factory_, "bar", 8);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, client0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Now everyone should get a split; the total weight is 10, 2 to foo and
  // 8 to bar, so foo should have 200 split between 2 requests, resulting in
  // 100 each, and bar should get 800 not split.
  EXPECT_EQ(100, client1.consume(1000));
  EXPECT_EQ(100, client2.consume(1000));
  EXPECT_EQ(800, client3.consume(1000));
}

TEST_F(FactoryTest, DeletingARequestCancelsItsShare) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 2);
  Client client2(*factory_, "foo", 2);
  absl::optional<Client> client3(std::in_place, *factory_, "bar", 8);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, client0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3->consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Cancel bar's request by deletion.
  client3.reset();
  // Now the other two should get a 50:50 split on the same tenant.
  EXPECT_EQ(500, client1.consume(1000));
  EXPECT_EQ(500, client2.consume(1000));
}

TEST_F(FactoryTest, PoorlyDividedSplitsStillEmptyTheBucket) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 1);
  Client client2(*factory_, "bar", 73);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, client0.consume(1000));
  // Queue a 1000 request for everyone else.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));

  // Each of them should get their expected amount maybe plus the indivisible
  // part. There is no hard rule about who gets the remainder.
  uint64_t remainder = 1000 % 74;
  uint64_t foo_fraction = 1000 / 74;
  uint64_t bar_fraction = 1000 / 74 * 73;
  EXPECT_EQ(foo_fraction + remainder, client1.consume(1000));
  EXPECT_EQ(bar_fraction, client2.consume(1000));
}

TEST_F(FactoryTest, PoorlyDividedSplitsCanBeSubdivided) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 1);
  Client client2(*factory_, "foo2", 1);
  Client client3(*factory_, "bar", 72);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, client0.consume(1000));
  // Queue a 1000 request for everyone else, except foo only wants to consume
  // a little bit of "indivisible spill".
  EXPECT_EQ(0, client1.consume(1000 / 74 + 2));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  // Each should have between their share and their share plus the remainder,
  // which gets given to whoever is earlier in the queue.
  // Since foo only wants 2, it takes only part of the remainder, with the
  // rest of the remainder going to foo2.
  uint64_t remainder = 1000 % 74;
  uint64_t foo_fraction = 1000 / 74;
  uint64_t bar_fraction = 1000 / 74 * 72;
  EXPECT_EQ(foo_fraction + 2, client1.consume(1000 / 74 + 2));
  EXPECT_EQ(foo_fraction + remainder - 2, client2.consume(1000));
  EXPECT_EQ(bar_fraction, client3.consume(1000));
}

TEST_F(FactoryTest, UnclaimedSharesCanStackUp) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 1);
  // Empty the bucket and queue up another 4000 for baz.
  EXPECT_EQ(1000, client0.consume(5000));
  // Put foo in the queue too.
  EXPECT_EQ(0, client1.consume(1));
  // Fill the bucket and take one token for foo (baz will get 999).
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  EXPECT_EQ(1, client1.consume(1));
  // Fill the bucket and put foo back in the queue (baz will get 1000).
  time_system_.setMonotonicTime(std::chrono::seconds(2));
  EXPECT_EQ(0, client1.consume(1));
  // Fill the bucket and take one token for foo (baz will get 999).
  time_system_.setMonotonicTime(std::chrono::seconds(3));
  EXPECT_EQ(1, client1.consume(1));
  // Baz finally gets around to requesting the tokens it didn't already
  // get, and has saved up 2998 of them.
  EXPECT_EQ(2998, client0.consume(4000));
}

TEST_F(FactoryTest, RequestForLessThanEarmarkedFractionFreesUpTokensForOtherRequestsOrTenants) {
  Client client0(*factory_, "baz", 1);
  Client client1(*factory_, "foo", 1);
  Client client2(*factory_, "foo", 1);
  Client client3(*factory_, "bar", 1);
  // Empty the bucket for baz.
  EXPECT_EQ(1000, client0.consume(1000));
  // Queue 100 for foo request 1, and 300 for foo request 2 - they will be splitting
  // 500, and should both be satisfied even though the fair share is only 250,
  // because there is enough left over from foo1 not claiming its entire share
  // for foo2 to get more.
  // Queue 1000 for bar - this should get 600 even though bar's share of 1000
  // is only 500, because foo only took 400 so bar can take the excess.
  EXPECT_EQ(0, client1.consume(100));
  EXPECT_EQ(0, client2.consume(300));
  EXPECT_EQ(0, client3.consume(1000));
  // Fill the bucket.
  time_system_.setMonotonicTime(std::chrono::seconds(1));
  uint64_t foo_gets = client1.consume(100);
  uint64_t foo2_gets = client2.consume(300);
  uint64_t bar_gets = client3.consume(1000);
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
