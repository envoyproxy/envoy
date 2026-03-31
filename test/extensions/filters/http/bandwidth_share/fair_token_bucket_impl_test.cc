#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

auto IsBetween = [](uint64_t min, uint64_t max) {
  return testing::AllOf(testing::Ge(min), testing::Le(max));
};

class ClientTest : public testing::Test {
  void SetUp() {
    // Avoid starting at time zero.
    time_system_.setMonotonicTime(start_time_);
  }

protected:
  MonotonicTime start_time_{std::chrono::seconds(5)};
  Event::SimulatedTimeSystem time_system_;
  std::chrono::milliseconds time_to_next_token_;
  std::shared_ptr<Bucket> bucket_ = Bucket::create(1000, time_system_);
};

TEST_F(ClientTest, NotImplementedConsumeWithTimeToNextToken) {
  Client client(bucket_, "foo", 1);
  std::chrono::milliseconds unused_out;
  EXPECT_ENVOY_BUG(client.consume(750, true, unused_out), "not expected");
}

TEST_F(ClientTest, NotImplementedConsumeWithAllowPartialFalse) {
  Client client(bucket_, "foo", 1);
  EXPECT_ENVOY_BUG(client.consume(750, false), "not expected");
}

TEST_F(ClientTest, NotImplementedNextTokenAvailable) {
  Client client(bucket_, "foo", 1);
  EXPECT_ENVOY_BUG(client.nextTokenAvailable(), "not expected");
}

TEST_F(ClientTest, MaybeResetDoesNothingAndThisTestJustProvidesCoverage) {
  Client client(bucket_, "foo", 1);
  client.maybeReset(12345);
}

TEST_F(ClientTest, InitializationAndTimeBothFillTheBucket) {
  Client client(bucket_, "foo", 1);
  EXPECT_EQ(750, client.consume(750));
  // Can get to a total of 950 before limiting kicks in.
  EXPECT_EQ(200, client.consume(750));
  // With only one client, can get the other 50 tokens in one shot while limiting.
  EXPECT_EQ(50, client.consume(500));
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(1));
  // When the bucket has refilled, all tokens are available again.
  EXPECT_EQ(950, client.consume(950));
}

TEST_F(ClientTest, CompetingRequestsGetAppropriateShares) {
  Client client0(bucket_, "baz", 1);
  Client client1(bucket_, "foo", 2);
  Client client2(bucket_, "foo", 2);
  Client client3(bucket_, "bar", 8);
  // Empty the free tokens for baz.
  EXPECT_EQ(950, client0.consume(950));
  // Put everyone else in the queue.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3.consume(1000));
  // They should each get a weighted share of the 50 tokens.
  EXPECT_EQ(5, client1.consume(1000));
  EXPECT_EQ(5, client2.consume(1000));
  EXPECT_EQ(40, client3.consume(1000));
  // Advance one fill-interval.
  time_system_.advanceTimeAsyncImpl(std::chrono::milliseconds(50));
  // Now they should each get an equal share of the new 50 tokens.
  EXPECT_EQ(5, client1.consume(1000));
  EXPECT_EQ(5, client2.consume(1000));
  EXPECT_EQ(40, client3.consume(1000));
}

TEST_F(ClientTest, DeletingARequestCancelsItsShare) {
  Client client0(bucket_, "baz", 1);
  Client client1(bucket_, "foo", 2);
  Client client2(bucket_, "foo", 2);
  absl::optional<Client> client3(std::in_place, bucket_, "bar", 8);
  // Empty the loose bucket for baz.
  EXPECT_EQ(950, client0.consume(950));
  // Put everyone else in the queue.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3->consume(1000));
  // They should each get a weighted share.
  EXPECT_EQ(5, client1.consume(1000));
  EXPECT_EQ(5, client2.consume(1000));
  EXPECT_EQ(40, client3->consume(1000));
  // Cancel bar's request by deletion.
  client3.reset();
  // Pass a fill-interval and let the drain apply for bar so it's removed from the queue.
  time_system_.advanceTimeAsyncImpl(std::chrono::milliseconds(50));
  // Now the other two should get a 50:50 split on the same tenant.
  EXPECT_EQ(25, client1.consume(1000));
  EXPECT_EQ(25, client2.consume(1000));
}

TEST_F(ClientTest, ClientDrainedCancelsItsShare) {
  Client client0(bucket_, "baz", 1);
  Client client1(bucket_, "foo", 2);
  Client client2(bucket_, "foo", 2);
  Client client3(bucket_, "bar", 8);
  // Empty the loose bucket for baz.
  EXPECT_EQ(950, client0.consume(950));
  // Put everyone else in the queue.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  EXPECT_EQ(0, client3.consume(1000));
  // They should each get a weighted share.
  EXPECT_EQ(5, client1.consume(1000));
  EXPECT_EQ(5, client2.consume(1000));
  EXPECT_EQ(40, client3.consume(40));
  // bar should be removed from the queue due to receiving all the tokens it wanted.
  // Pass a fill-interval and let the drain apply for bar so it's removed from the queue.
  time_system_.advanceTimeAsyncImpl(std::chrono::milliseconds(50));
  // Now the other two should get a 50:50 split on the same tenant.
  EXPECT_EQ(25, client1.consume(1000));
  EXPECT_EQ(25, client2.consume(1000));
}

TEST_F(ClientTest, PoorlyDividedSplitsStillEmptyTheBucket) {
  Client client0(bucket_, "baz", 1);
  Client client1(bucket_, "foo", 1);
  Client client2(bucket_, "bar", 22);
  // Empty the bucket for baz.
  EXPECT_EQ(950, client0.consume(950));
  // Put everyone else in the queue.
  EXPECT_EQ(0, client1.consume(1000));
  EXPECT_EQ(0, client2.consume(1000));
  // They should each get their expected share of 50.
  EXPECT_EQ(2, client1.consume(1000));
  EXPECT_EQ(47, client2.consume(1000));
  // advance by a fill_interval.
  time_system_.advanceTimeAsyncImpl(std::chrono::milliseconds(50));

  // Now they should each get their expected share but the first client
  // also gets the remainder from last time.
  EXPECT_EQ(3, client1.consume(1000));
  EXPECT_EQ(47, client2.consume(1000));
}

TEST_F(ClientTest, RunWithAggressiveThreadsToEnsureNoDeadlocks) {
  absl::Mutex mu;
  uint64_t total_consumed = 0;
  struct ThreadData {
    std::thread thread;
    uint64_t consumed = 0;
    bool acting = true;
    absl::optional<Client> client;
  } threads[10];
  for (size_t i = 0; i < 10; i++) {
    threads[i].client.emplace(bucket_, "foo", 1);
    threads[i].thread = std::thread([&mu, &total_consumed, thread = &threads[i]] {
      while (true) {
        uint64_t got = thread->client->consume(1000);
        {
          absl::MutexLock lock(mu);
          thread->acting = false;
          thread->consumed += got;
          total_consumed += got;
          if (total_consumed >= 100000) {
            break;
          }
          mu.Await(absl::Condition(&thread->acting));
        }
      }
    });
  }
  const auto all_threads_blocked = [&]() {
    for (int i = 0; i < 10; i++) {
      if (threads[i].acting) {
        return false;
      }
    }
    return true;
  };
  while (true) {
    absl::MutexLock lock(mu);
    // Wait for all threads to have consumed, then advance time.
    mu.Await(absl::Condition(&all_threads_blocked));
    for (int i = 0; i < 10; i++) {
      threads[i].acting = true;
    }
    if (total_consumed >= 100000) {
      break;
    }
    time_system_.advanceTimeWaitImpl(std::chrono::milliseconds(50));
  }
  for (size_t i = 0; i < 10; i++) {
    threads[i].thread.join();
  }
  // Since it's battling threads, and one of them got the initial bucket fill,
  // an exact fair share cannot be expected, so just verify that
  // everyone got a reasonable share.
  absl::MutexLock lock(mu);
  for (size_t i = 0; i < 10; i++) {
    EXPECT_THAT(threads[i].consumed, IsBetween(9000, 11000));
  }
  // We definitely shouldn't have granted more than 100000 tokens in less
  // than 99 fake-seconds.
  EXPECT_THAT(
      std::chrono::duration_cast<std::chrono::seconds>(time_system_.monotonicTime() - start_time_)
          .count(),
      IsBetween(99, 101));
}

} // namespace FairTokenBucket
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
