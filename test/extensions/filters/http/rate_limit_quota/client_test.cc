#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"
#include "envoy/type/v3/token_bucket.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/thread_local/thread_local.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"
#include "test/extensions/filters/http/rate_limit_quota/client_test_utils.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "google/protobuf/util/message_differencer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using envoy::type::v3::RateLimitStrategy;
using envoy::type::v3::TokenBucket;
using google::protobuf::util::MessageDifferencer;
using testing::Unused;

// Setup cb to trigger when the bucket is created & usage reported.
class GlobalClientCallbacks : public GlobalRateLimitClientCallbacks {
 public:
  void onBucketCreated([[maybe_unused]] const BucketId& bucket_id, size_t id) override {
    expected_buckets.at(id)->Notify();
  };
  void onUsageReportsSent() override { report_sent->Notify(); };
  void onQuotaResponseProcessed() override { response_processed->Notify(); };

  std::unique_ptr<absl::Notification> report_sent =
      std::make_unique<absl::Notification>();
  std::unique_ptr<absl::Notification> response_processed =
      std::make_unique<absl::Notification>();

  using ExpectedBuckets =
      absl::flat_hash_map<size_t, std::unique_ptr<absl::Notification>>;
  void expectBuckets(std::vector<size_t> ids) {
    expected_buckets = ExpectedBuckets();
    for (size_t id : ids) {
      expected_buckets[id] = std::make_unique<absl::Notification>();
    }
  }

  void waitForExpectedBuckets(absl::Duration timeout = absl::Seconds(5)) {
    for (const auto& [id, notif] : expected_buckets) {
      EXPECT_TRUE(notif->WaitForNotificationWithTimeout(timeout));
      expected_buckets[id] = std::make_unique<absl::Notification>();
    }
  }

 private:
  ExpectedBuckets expected_buckets;
};

inline void waitForNotification(std::unique_ptr<absl::Notification>& notif,
                                absl::Duration timeout = absl::Seconds(5)) {
  EXPECT_TRUE(notif->WaitForNotificationWithTimeout(timeout));
  notif = std::make_unique<absl::Notification>();
}

// Directly exercise the internal stream management done by the global client.
class GlobalClientTest : public ::testing::Test {
 protected:
  GlobalClientTest() {
    (*sample_bucket_id_.mutable_bucket())["mock_id_key"] = "mock_id_value";
    (*sample_bucket_id_.mutable_bucket())["mock_id_key2"] = "mock_id_value2";
    sample_id_hash_ = MessageUtil::hash(sample_bucket_id_);
    default_allow_action = buildBlanketAction(sample_bucket_id_, false);
  }

  void SetUp() override {
    mock_stream_client = std::make_unique<RateLimitTestClient>();
    buckets_tls_ =
        std::make_unique<ThreadLocal::TypedSlot<ThreadLocalBucketsCache>>(
            mock_stream_client->context_.server_factory_context_.thread_local_);
    auto initial_tl_buckets_cache = std::make_shared<ThreadLocalBucketsCache>(
        std::make_shared<BucketsCache>());
    buckets_tls_->set(
        [initial_tl_buckets_cache]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
          return initial_tl_buckets_cache;
        });

    mock_stream_client->expectClientCreation();
    global_client_ = std::make_shared<GlobalRateLimitClientImpl>(
        mock_stream_client->config_with_hash_key_, mock_stream_client->context_,
        mock_domain_, reporting_interval_, *buckets_tls_,
        *mock_stream_client->dispatcher_);
    // Set callbacks to handle asynchronous timing.
    auto callbacks = std::make_unique<GlobalClientCallbacks>();
    cb_ptr_ = callbacks.get();
    global_client_->setCallbacks(std::move(callbacks));

    unordered_differencer_.set_repeated_field_comparison(
        MessageDifferencer::AS_SET);
  }

  std::unique_ptr<RateLimitTestClient> mock_stream_client = nullptr;
  std::shared_ptr<GlobalRateLimitClientImpl> global_client_ = nullptr;
  ThreadLocal::TypedSlotPtr<ThreadLocalBucketsCache> buckets_tls_ = nullptr;
  GlobalClientCallbacks* cb_ptr_ = nullptr;

  // Statics
  std::string mock_domain_ = "mock_rlqs";
  std::chrono::milliseconds reporting_interval_ =
      std::chrono::milliseconds(10000);
  BucketAction default_allow_action;

  BucketId sample_bucket_id_;
  size_t sample_id_hash_;

  MessageDifferencer unordered_differencer_;

  struct reportData {
    int allowed;
    int denied;
    BucketId bucket_id;
  };
  RateLimitQuotaUsageReports buildReports(
      const std::vector<reportData>& test_reports) {
    RateLimitQuotaUsageReports reports;
    reports.set_domain(mock_domain_);

    for (const auto& test_report : test_reports) {
      auto* report = reports.add_bucket_quota_usages();
      report->set_num_requests_allowed(test_report.allowed);
      report->set_num_requests_denied(test_report.denied);
      report->mutable_bucket_id()->CopyFrom(test_report.bucket_id);
    }
    return reports;
  }

  BucketAction buildBlanketAction(const BucketId& bucket_id, bool deny_all) {
    BucketAction action = buildBlanketAction(deny_all);
    auto* quota = action.mutable_quota_assignment_action();
    quota->mutable_assignment_time_to_live()->set_seconds(120);
    action.mutable_bucket_id()->CopyFrom(bucket_id);
    return action;
  }

  BucketAction buildBlanketAction(bool deny_all) {
    BucketAction action;
    auto* quota = action.mutable_quota_assignment_action();
    quota->mutable_rate_limit_strategy()->set_blanket_rule(
        deny_all ? RateLimitStrategy::DENY_ALL : RateLimitStrategy::ALLOW_ALL);
    return action;
  }

  BucketAction buildTokenBucketAction(const BucketId& bucket_id,
                                      uint32_t max_tokens,
                                      uint32_t tokens_per_fill,
                                      std::chrono::seconds fill_interval) {
    BucketAction action =
        buildTokenBucketAction(max_tokens, tokens_per_fill, fill_interval);
    action.mutable_bucket_id()->CopyFrom(bucket_id);
    action.mutable_quota_assignment_action()
        ->mutable_assignment_time_to_live()
        ->set_seconds(120);

    return action;
  }

  BucketAction buildTokenBucketAction(uint32_t max_tokens,
                                      uint32_t tokens_per_fill,
                                      std::chrono::seconds fill_interval) {
    BucketAction action;
    TokenBucket* token_bucket = action.mutable_quota_assignment_action()
                                    ->mutable_rate_limit_strategy()
                                    ->mutable_token_bucket();
    token_bucket->set_max_tokens(max_tokens);
    token_bucket->mutable_tokens_per_fill()->set_value(tokens_per_fill);
    token_bucket->mutable_fill_interval()->set_seconds(fill_interval.count());
    return action;
  }
};

void setAtomic(uint64_t value, std::atomic<uint64_t>& counter) {
  uint64_t loaded = counter.load(std::memory_order_relaxed);
  while (!counter.compare_exchange_weak(loaded, value,
                                        std::memory_order_relaxed)) {
  }
}

// Helpers for getting references to indices in the cached buckets in TLS.
absl::StatusOr<std::shared_ptr<CachedBucket>> tryGetBucket(
    ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls, size_t id) {
  auto cache_ref = buckets_tls.get();
  if (!cache_ref.has_value() || !cache_ref->quota_buckets_)
    return absl::NotFoundError("Bucket TLS not initialized");

  auto bucket_it = cache_ref->quota_buckets_->find(id);
  if (bucket_it == cache_ref->quota_buckets_->end())
    return absl::NotFoundError("Bucket not found");

  return bucket_it->second;
}
void getBucket(ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
               size_t id, std::shared_ptr<CachedBucket>& bucket_out) {
  auto cache_ref = buckets_tls.get();
  ASSERT_TRUE(cache_ref.has_value());
  ASSERT_TRUE(cache_ref->quota_buckets_);

  auto bucket_it = cache_ref->quota_buckets_->find(id);
  ASSERT_NE(bucket_it, cache_ref->quota_buckets_->end());

  bucket_out = bucket_it->second;
}
std::shared_ptr<CachedBucket> getBucket(
    ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls, size_t id) {
  std::shared_ptr<CachedBucket> bucket;
  getBucket(buckets_tls, id, bucket);
  return bucket;
}
void getQuotaUsage(ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls,
                   size_t id, std::shared_ptr<QuotaUsage>* quota_usage_out) {
  std::shared_ptr<CachedBucket> bucket = getBucket(buckets_tls, id);
  ASSERT_TRUE(bucket);
  ASSERT_TRUE(bucket->quota_usage);

  *quota_usage_out = bucket->quota_usage;
}
std::shared_ptr<QuotaUsage> getQuotaUsage(
    ThreadLocal::TypedSlot<ThreadLocalBucketsCache>& buckets_tls, size_t id) {
  std::shared_ptr<QuotaUsage> quota_usage;
  getQuotaUsage(buckets_tls, id, &quota_usage);
  return quota_usage;
}

TEST_F(GlobalClientTest, TestInitialCreation) {
  // When the first bucket creation comes in, the global client starts its
  // internal stream & reporting timer.
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  // The global client should handle multiple, duplicate createBucket calls
  // correctly as multiple worker threads may attempt to create the same bucket
  // concurrently.
  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  // Expect the bucket cache to update with a new bucket quickly.
  cb_ptr_->waitForExpectedBuckets();
  auto cache_ref = buckets_tls_->get();
  ASSERT_TRUE(cache_ref.has_value());
  ASSERT_TRUE(cache_ref->quota_buckets_);
  ASSERT_EQ(cache_ref->quota_buckets_->size(), 1);
  auto bucket_it = cache_ref->quota_buckets_->find(sample_id_hash_);
  // Check that the expected bucket action & static id are correct per defaults.
  ASSERT_NE(bucket_it, cache_ref->quota_buckets_->end());

  EXPECT_TRUE(unordered_differencer_.Equals(bucket_it->second->bucket_id,
                                            sample_bucket_id_));
  EXPECT_TRUE(unordered_differencer_.Equals(bucket_it->second->bucket_action,
                                            default_allow_action));

  // Pull the atomics to ensure that they initialized & incremented correctly.
  ASSERT_TRUE(bucket_it->second->quota_usage);
  uint64_t allowed = bucket_it->second->quota_usage->num_requests_allowed.load(
      std::memory_order_relaxed);
  uint64_t denied = bucket_it->second->quota_usage->num_requests_denied.load(
      std::memory_order_relaxed);
  EXPECT_GT(allowed, 0);
  EXPECT_LT(allowed, 4);
  EXPECT_EQ(denied, 0);
}

// Test with a non-allow-all no_assignment_behavior for use during bucket
// creation.
TEST_F(GlobalClientTest, TestCreationWithDefaultDeny) {
  // When the first bucket creation comes in, the global client starts its
  // internal stream & reporting timer.
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  BucketAction default_deny_action = default_allow_action;
  default_deny_action.mutable_quota_assignment_action()
      ->mutable_rate_limit_strategy()
      ->set_blanket_rule(RateLimitStrategy::DENY_ALL);

  // The global client should handle multiple, duplicate createBucket calls
  // correctly as multiple worker threads may attempt to create the same bucket
  // concurrently.
  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_deny_action, false);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_deny_action, false);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_deny_action, false);
  // Expect the bucket cache to update with a new bucket quickly.
  cb_ptr_->waitForExpectedBuckets();
  auto cache_ref = buckets_tls_->get();
  ASSERT_TRUE(cache_ref.has_value());
  ASSERT_TRUE(cache_ref->quota_buckets_);
  ASSERT_EQ(cache_ref->quota_buckets_->size(), 1);
  auto bucket_it = cache_ref->quota_buckets_->find(sample_id_hash_);
  // Check that the expected bucket action & static id are correct per defaults.
  ASSERT_NE(bucket_it, cache_ref->quota_buckets_->end());

  EXPECT_TRUE(unordered_differencer_.Equals(bucket_it->second->bucket_id,
                                            sample_bucket_id_));
  EXPECT_TRUE(unordered_differencer_.Equals(bucket_it->second->bucket_action,
                                            default_deny_action));

  // Pull the atomics to ensure that they initialized & incremented correctly.
  ASSERT_TRUE(bucket_it->second->quota_usage);
  uint64_t allowed = bucket_it->second->quota_usage->num_requests_allowed.load(
      std::memory_order_relaxed);
  uint64_t denied = bucket_it->second->quota_usage->num_requests_denied.load(
      std::memory_order_relaxed);
  EXPECT_GT(denied, 0);
  EXPECT_LT(denied, 4);
  EXPECT_EQ(allowed, 0);
}

// TODO: move tests to use notifications / events during expected calls to
// trigger continuation of a test, instead of raw sleeps.
TEST_F(GlobalClientTest, BasicUsageReporting) {
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  cb_ptr_->waitForExpectedBuckets();
  // Get bucket from TLS.
  std::shared_ptr<QuotaUsage> quota_usage =
      getQuotaUsage(*buckets_tls_, sample_id_hash_);
  setAtomic(1, quota_usage->num_requests_allowed);
  setAtomic(2, quota_usage->num_requests_denied);

  RateLimitQuotaUsageReports expected_reports =
      buildReports(std::vector<reportData>{
          {.allowed = 1, .denied = 2, .bucket_id = sample_bucket_id_}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false))
      .Times(1);

  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);
  // After the expected report goes out, the atomics should be reset for the
  // next aggregation interval.
  quota_usage = getQuotaUsage(*buckets_tls_, sample_id_hash_);
  EXPECT_EQ(quota_usage->num_requests_allowed.load(std::memory_order_relaxed),
            0);
  EXPECT_EQ(quota_usage->num_requests_denied.load(std::memory_order_relaxed),
            0);
}

// The usage reporting timer handles stream retries while it's inactive (either
// due to initial failure or failure of an existing stream).
TEST_F(GlobalClientTest, TestStreamCreationFailures) {
  // Expect stream to start on the 4th attempt and drop the 2 reports that are
  // generated before the stream comes up.
  mock_stream_client->expectStreamCreation(4);
  mock_stream_client->setStreamStartToFail(3);
  // Stream failure should not stop the usage reporting timer.
  mock_stream_client->expectTimerCreation(reporting_interval_);

  // Only the first createBucket should result in a stream attempt, as only the
  // reporting timer reattempts stream creation from then on.
  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  cb_ptr_->waitForExpectedBuckets();
  // Bucket should be created, even with the stream failure.
  std::shared_ptr<QuotaUsage> quota_usage =
      getQuotaUsage(*buckets_tls_, sample_id_hash_);
  EXPECT_GT(quota_usage->num_requests_allowed, 0);
  EXPECT_LT(quota_usage->num_requests_allowed, 4);
  // With the timer cb, the stream should be reattempted, fail starting and
  // cause the generated report to drop.
  for (int i = 0; i < 2; ++i) {
    mock_stream_client->timer_->invokeCallback();
    waitForNotification(cb_ptr_->report_sent);
    // Refresh state from the buckets cache in TLS. Expect the atomics to have
    // reset after the dropped reports.
    quota_usage = getQuotaUsage(*buckets_tls_, sample_id_hash_);
    EXPECT_EQ(quota_usage->num_requests_allowed, 0);
    setAtomic(4 + i, quota_usage->num_requests_allowed);
  }
  // A fourth stream creation should succeed & result in a sent usage report.
  RateLimitQuotaUsageReports expected_reports =
      buildReports(std::vector<reportData>{
          {.allowed = 5, .denied = 0, .bucket_id = sample_bucket_id_}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false));

  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);
  quota_usage = getQuotaUsage(*buckets_tls_, sample_id_hash_);
  EXPECT_EQ(quota_usage->num_requests_allowed.load(std::memory_order_relaxed),
            0);
}

TEST_F(GlobalClientTest, TestStreamFailureMidUse) {
  // Expect exactly 2 steam creations, one at the beginning and once after the
  // stream closes, when the following usage reporting cycle opens another.
  mock_stream_client->expectStreamCreation(2);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  // Initial bucket creation & setting of usage data.
  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  cb_ptr_->waitForExpectedBuckets();
  // Get bucket from TLS.
  std::shared_ptr<QuotaUsage> quota_usage =
      getQuotaUsage(*buckets_tls_, sample_id_hash_);
  setAtomic(1, quota_usage->num_requests_allowed);
  setAtomic(2, quota_usage->num_requests_denied);

  RateLimitQuotaUsageReports expected_reports =
      buildReports(std::vector<reportData>{
          {.allowed = 1, .denied = 2, .bucket_id = sample_bucket_id_}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false));
  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);

  // After the expected report goes out, the atomics should be reset for the
  // next aggregation interval.
  quota_usage = getQuotaUsage(*buckets_tls_, sample_id_hash_);
  EXPECT_EQ(quota_usage->num_requests_allowed.load(std::memory_order_relaxed),
            0);
  EXPECT_EQ(quota_usage->num_requests_denied.load(std::memory_order_relaxed),
            0);
  // Close the stream to show the internal restart mechanism.
  global_client_->onRemoteClose(
      Grpc::Status::Canceled,
      "Stream cancelled gracefully during RLQS server shutdown.");

  // Should still be able to create buckets safely & interact with atomics.
  BucketId sample_bucket_id2;
  (*sample_bucket_id2.mutable_bucket())["mock_id_key"] = "mutable_id_value3";
  (*sample_bucket_id2.mutable_bucket())["mock_id_key2"] = "mutable_id_value4";
  size_t sample_id_hash2 = MessageUtil::hash(sample_bucket_id2);
  cb_ptr_->expectBuckets({sample_id_hash2});
  BucketAction default_allow_action2 = default_allow_action;
  *default_allow_action2.mutable_bucket_id() = sample_bucket_id2;
  global_client_->createBucket(sample_bucket_id2, sample_id_hash2,
                               default_allow_action2, true);
  global_client_->createBucket(sample_bucket_id2, sample_id_hash2,
                               default_allow_action2, true);
  setAtomic(3, quota_usage->num_requests_allowed);
  setAtomic(4, quota_usage->num_requests_denied);

  // Wait for the second bucket creation to complete.
  cb_ptr_->waitForExpectedBuckets();
  quota_usage = getQuotaUsage(*buckets_tls_, sample_id_hash_);
  std::shared_ptr<QuotaUsage> quota_usage2 =
      getQuotaUsage(*buckets_tls_, sample_id_hash2);
  EXPECT_EQ(quota_usage->num_requests_allowed.load(std::memory_order_relaxed),
            3);
  EXPECT_EQ(quota_usage->num_requests_denied.load(std::memory_order_relaxed),
            4);
  EXPECT_EQ(quota_usage2->num_requests_allowed.load(std::memory_order_relaxed),
            2);
  EXPECT_EQ(quota_usage2->num_requests_denied.load(std::memory_order_relaxed),
            0);

  // Expect stream creation & successful sending of a report.
  expected_reports = buildReports(std::vector<reportData>{
      {.allowed = 3, .denied = 4, .bucket_id = sample_bucket_id_},
      {.allowed = 2, .denied = 0, .bucket_id = sample_bucket_id2}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false));
  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);
}

TEST_F(GlobalClientTest, TestBasicResponseProcessing) {
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  BucketId sample_bucket_id2;
  (*sample_bucket_id2.mutable_bucket())["mock_id_key"] = "mutable_id_value3";
  (*sample_bucket_id2.mutable_bucket())["mock_id_key2"] = "mutable_id_value4";
  size_t sample_id_hash2 = MessageUtil::hash(sample_bucket_id2);
  BucketId sample_bucket_id3;
  (*sample_bucket_id3.mutable_bucket())["mock_id_key"] = "mutable_id_value5";
  (*sample_bucket_id3.mutable_bucket())["mock_id_key2"] = "mutable_id_value6";
  size_t sample_id_hash3 = MessageUtil::hash(sample_bucket_id3);

  BucketAction default_allow_action2 = default_allow_action;
  *default_allow_action2.mutable_bucket_id() = sample_bucket_id2;
  BucketAction default_allow_action3 = default_allow_action;
  *default_allow_action3.mutable_bucket_id() = sample_bucket_id3;

  cb_ptr_->expectBuckets({sample_id_hash_, sample_id_hash2, sample_id_hash3});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  global_client_->createBucket(sample_bucket_id2, sample_id_hash2,
                               default_allow_action2, true);
  global_client_->createBucket(sample_bucket_id3, sample_id_hash3,
                               default_allow_action3, true);
  cb_ptr_->waitForExpectedBuckets();

  RateLimitQuotaUsageReports expected_reports =
      buildReports(std::vector<reportData>{
          {.allowed = 1, .denied = 0, .bucket_id = sample_bucket_id_},
          {.allowed = 1, .denied = 0, .bucket_id = sample_bucket_id2},
          {.allowed = 1, .denied = 0, .bucket_id = sample_bucket_id3}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false));

  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);

  // Test deny-all, allow-all and token-bucket response handling.
  auto deny_action = buildBlanketAction(sample_bucket_id_, true);
  auto allow_action = buildBlanketAction(sample_bucket_id2, false);
  auto token_bucket_action = buildTokenBucketAction(sample_bucket_id3, 300, 60,
                                                    std::chrono::seconds(12));
  std::unique_ptr<RateLimitQuotaResponse> response =
      std::make_unique<RateLimitQuotaResponse>();
  response->add_bucket_action()->CopyFrom(deny_action);
  response->add_bucket_action()->CopyFrom(allow_action);
  response->add_bucket_action()->CopyFrom(token_bucket_action);

  // Send the response across the stream.
  global_client_->onReceiveMessage(std::move(response));
  waitForNotification(cb_ptr_->response_processed);

  // Expect the buckets in TLS to have matching assignments.
  std::shared_ptr<CachedBucket> deny_all_bucket =
      getBucket(*buckets_tls_, sample_id_hash_);
  EXPECT_TRUE(unordered_differencer_.Equals(deny_all_bucket->bucket_action,
                                            deny_action));

  std::shared_ptr<CachedBucket> allow_all_bucket =
      getBucket(*buckets_tls_, sample_id_hash2);
  EXPECT_TRUE(unordered_differencer_.Equals(allow_all_bucket->bucket_action,
                                            allow_action));

  std::shared_ptr<CachedBucket> token_bucket =
      getBucket(*buckets_tls_, sample_id_hash3);
  EXPECT_TRUE(unordered_differencer_.Equals(token_bucket->bucket_action,
                                            token_bucket_action));
}

// Expect assignments that don't match to any cached buckets to be dropped from
// assignments without damaging the envoy.
TEST_F(GlobalClientTest, TestResponseProcessingForNonExistentBucket) {
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  // sample_bucket_id2 will not be added to the bucket cache via reporting so we
  // expect an assignment from the response to be dropped.
  BucketId sample_bucket_id2;
  (*sample_bucket_id2.mutable_bucket())["mock_id_key"] = "mutable_id_value3";
  (*sample_bucket_id2.mutable_bucket())["mock_id_key2"] = "mutable_id_value4";
  size_t sample_id_hash2 = MessageUtil::hash(sample_bucket_id2);

  cb_ptr_->expectBuckets({sample_id_hash_});
  global_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                               default_allow_action, true);
  cb_ptr_->waitForExpectedBuckets();

  RateLimitQuotaUsageReports expected_reports =
      buildReports(std::vector<reportData>{
          {.allowed = 1, .denied = 0, .bucket_id = sample_bucket_id_}});
  EXPECT_CALL(mock_stream_client->stream_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(
                                  expected_reports),
                              false));

  mock_stream_client->timer_->invokeCallback();
  waitForNotification(cb_ptr_->report_sent);

  EXPECT_OK(tryGetBucket(*buckets_tls_, sample_id_hash_));
  EXPECT_FALSE(tryGetBucket(*buckets_tls_, sample_id_hash2).ok());

  auto deny_action = buildBlanketAction(sample_bucket_id_, true);
  auto allow_action = buildBlanketAction(sample_bucket_id2, false);
  std::unique_ptr<RateLimitQuotaResponse> response =
      std::make_unique<RateLimitQuotaResponse>();
  response->add_bucket_action()->CopyFrom(deny_action);
  response->add_bucket_action()->CopyFrom(allow_action);

  // Send the response across the stream.
  global_client_->onReceiveMessage(std::move(response));
  waitForNotification(cb_ptr_->response_processed);

  // Expect the second bucket hash to not be in the bucket cache as it wasn't
  // there before the response included it.
  std::shared_ptr<CachedBucket> deny_all_bucket =
      getBucket(*buckets_tls_, sample_id_hash_);
  EXPECT_TRUE(unordered_differencer_.Equals(deny_all_bucket->bucket_action,
                                            deny_action));

  absl::StatusOr<std::shared_ptr<CachedBucket>> allow_all_bucket =
      tryGetBucket(*buckets_tls_, sample_id_hash2);
  EXPECT_FALSE(allow_all_bucket.ok());
}

class LocalClientTest : public GlobalClientTest {
 protected:
  LocalClientTest() : GlobalClientTest() {}

  void SetUp() override {
    GlobalClientTest::SetUp();
    // Initialize the TLS slot.
    client_tls_ = std::make_unique<
        ThreadLocal::TypedSlot<ThreadLocalGlobalRateLimitClientImpl>>(
        mock_stream_client->context_.server_factory_context_.thread_local_);
    // Create a ThreadLocal wrapper for the global client initialized in the
    // GlobalClientTest.
    auto tl_global_client =
        std::make_shared<ThreadLocalGlobalRateLimitClientImpl>(global_client_);
    // Set the TLS slot to return copies of the shared_ptr holding that
    // ThreadLocal object.
    client_tls_->set([tl_global_client]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
      return tl_global_client;
    });

    // Create the local client for testing.
    local_client_ = std::make_unique<LocalRateLimitClientImpl>(
        *client_tls_, *buckets_tls_);
  }

  std::unique_ptr<LocalRateLimitClientImpl> local_client_ = nullptr;
  ThreadLocal::TypedSlotPtr<ThreadLocalGlobalRateLimitClientImpl> client_tls_ =
      nullptr;
};

TEST_F(LocalClientTest, TestLocalClient) {
  // getBucket is a read-op that should only read from the bucket cache and
  // shouldn't need to send to the global client for anything.
  EXPECT_EQ(local_client_->getBucket(sample_id_hash_), nullptr);

  // When the filter calls to the local client to create a bucket though, that
  // should be passed up to the global client as it is a write-op.
  // As a result, we should expect full global client processing of a new
  // bucket.
  mock_stream_client->expectStreamCreation(1);
  mock_stream_client->expectTimerCreation(reporting_interval_);

  cb_ptr_->expectBuckets({sample_id_hash_});
  local_client_->createBucket(sample_bucket_id_, sample_id_hash_,
                              default_allow_action, true);
  cb_ptr_->waitForExpectedBuckets();

  // Now the local client should be able to see the newly created bucket in its
  // local TLS slot.
  std::shared_ptr<CachedBucket> bucket =
      local_client_->getBucket(sample_id_hash_);
  ASSERT_TRUE(bucket);

  EXPECT_TRUE(unordered_differencer_.Equals(bucket->bucket_action,
                                            default_allow_action));
}

}  // namespace
}  // namespace RateLimitQuota
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
