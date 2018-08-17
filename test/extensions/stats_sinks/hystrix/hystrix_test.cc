#include <chrono>
#include <memory>
#include <sstream>

#include "extensions/stat_sinks/hystrix/hystrix.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {

class ClusterTestInfo {

public:
  ClusterTestInfo(const std::string cluster_name) : cluster_name_(cluster_name) {
    ON_CALL(cluster_, info()).WillByDefault(Return(cluster_info_ptr_));
    ON_CALL(*cluster_info_, name()).WillByDefault(testing::ReturnRefOfCopy(cluster_name_));
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(cluster_stats_scope_));

    // Set gauge value.
    membership_total_gauge_.name_ = "membership_total";
    ON_CALL(cluster_stats_scope_, gauge("membership_total"))
        .WillByDefault(ReturnRef(membership_total_gauge_));
    ON_CALL(membership_total_gauge_, value()).WillByDefault(Return(5));

    // Attach counters.
    setCounterForTest(success_counter_, "upstream_rq_2xx");
    setCounterForTest(error_5xx_counter_, "upstream_rq_5xx");
    setCounterForTest(retry_5xx_counter_, "retry.upstream_rq_5xx");
    setCounterForTest(error_4xx_counter_, "upstream_rq_4xx");
    setCounterForTest(retry_4xx_counter_, "retry.upstream_rq_4xx");
    setCountersToZero();
  }

  // Attach the counter to cluster_stat_scope and set default value.
  void setCounterForTest(NiceMock<Stats::MockCounter>& counter, std::string counter_name) {
    counter.name_ = counter_name;
    ON_CALL(cluster_stats_scope_, counter(counter_name)).WillByDefault(ReturnRef(counter));
  }

  void setCountersToZero() {
    ON_CALL(error_5xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(retry_5xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(error_4xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(retry_4xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(success_counter_, value()).WillByDefault(Return(0));
  }

  // Set counter return values to simulate traffic
  void setCounterReturnValues(const uint64_t i, const uint64_t success_step,
                              const uint64_t error_4xx_step, const uint64_t error_4xx_retry_step,
                              const uint64_t error_5xx_step, const uint64_t error_5xx_retry_step,
                              const uint64_t timeout_step, const uint64_t timeout_retry_step,
                              const uint64_t rejected_step) {
    ON_CALL(error_5xx_counter_, value()).WillByDefault(Return((i + 1) * error_5xx_step));
    ON_CALL(retry_5xx_counter_, value()).WillByDefault(Return((i + 1) * error_5xx_retry_step));
    ON_CALL(error_4xx_counter_, value()).WillByDefault(Return((i + 1) * error_4xx_step));
    ON_CALL(retry_4xx_counter_, value()).WillByDefault(Return((i + 1) * error_4xx_retry_step));
    ON_CALL(success_counter_, value()).WillByDefault(Return((i + 1) * success_step));
    cluster_info_->stats().upstream_rq_timeout_.add(timeout_step);
    cluster_info_->stats().upstream_rq_per_try_timeout_.add(timeout_retry_step);
    cluster_info_->stats().upstream_rq_pending_overflow_.add(rejected_step);
  }

  NiceMock<Upstream::MockCluster> cluster_;
  Upstream::MockClusterInfo* cluster_info_ = new NiceMock<Upstream::MockClusterInfo>();
  Upstream::ClusterInfoConstSharedPtr cluster_info_ptr_{cluster_info_};

  NiceMock<Stats::MockStore> stats_store_;
  NiceMock<Stats::MockStore> cluster_stats_scope_;
  const std::string cluster_name_;

  NiceMock<Stats::MockGauge> membership_total_gauge_;
  NiceMock<Stats::MockCounter> success_counter_;
  NiceMock<Stats::MockCounter> error_5xx_counter_;
  NiceMock<Stats::MockCounter> retry_5xx_counter_;
  NiceMock<Stats::MockCounter> error_4xx_counter_;
  NiceMock<Stats::MockCounter> retry_4xx_counter_;
};

class HystrixSinkTest : public testing::Test {
public:
  HystrixSinkTest() { sink_.reset(new HystrixSink(server_, window_size_)); }

  Buffer::OwnedImpl createClusterAndCallbacks() {
    // Set cluster.
    cluster_map_.emplace(cluster1_name_, cluster1_.cluster_);
    ON_CALL(server_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));

    Buffer::OwnedImpl buffer;
    auto encode_callback = [&buffer](Buffer::Instance& data, bool) {
      // Set callbacks to send data to buffer. This will append to the end of the buffer, so
      // multiple calls will all be dumped one after another into this buffer.
      buffer.add(data);
    };
    ON_CALL(callbacks_, encodeData(_, _)).WillByDefault(Invoke(encode_callback));
    return buffer;
  }

  void addClusterToMap(const std::string& cluster_name, NiceMock<Upstream::MockCluster>& cluster) {
    cluster_map_.emplace(cluster_name, cluster);
    // Redefining since cluster_map_ is returned by value.
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
  }

  void removeClusterFromMap(const std::string& cluster_name) {
    cluster_map_.erase(cluster_name);
    // Redefining since cluster_map_ is returned by value.
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
  }

  void addSecondClusterHelper(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    cluster2_.setCountersToZero();
    addClusterToMap(cluster2_name_, cluster2_.cluster_);
  }

  std::unordered_map<std::string, std::string>
  addSecondClusterAndSendDataHelper(Buffer::OwnedImpl& buffer, const uint64_t success_step,
                                    const uint64_t error_step, const uint64_t timeout_step,
                                    const uint64_t success_step2, const uint64_t error_step2,
                                    const uint64_t timeout_step2) {

    // Add new cluster.
    addSecondClusterHelper(buffer);

    // Generate data to both clusters.
    for (uint64_t i = 0; i < (window_size_ + 1); i++) {
      buffer.drain(buffer.length());
      cluster1_.setCounterReturnValues(i, success_step, error_step, 0, 0, 0, timeout_step, 0, 0);
      cluster2_.setCounterReturnValues(i, success_step2, error_step2, 0, 0, 0, timeout_step2, 0, 0);
      sink_->flush(source_);
    }

    return buildClusterMap(buffer.toString());
  }

  void removeSecondClusterHelper(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    removeClusterFromMap(cluster2_name_);
    sink_->flush(source_);
  }

  void validateResults(const std::string& data_message, uint64_t success_step, uint64_t error_step,
                       uint64_t timeout_step, uint64_t timeout_retry_step, uint64_t rejected_step,
                       uint64_t window_size) {
    // Convert to json object.
    Json::ObjectSharedPtr json_data_message = Json::Factory::loadFromString(data_message);
    EXPECT_EQ(json_data_message->getInteger("rollingCountSemaphoreRejected"),
              (window_size * rejected_step))
        << "window_size=" << window_size << ", rejected_step=" << rejected_step;
    EXPECT_EQ(json_data_message->getInteger("rollingCountSuccess"), (window_size * success_step))
        << "window_size=" << window_size << ", success_step=" << success_step;
    EXPECT_EQ(json_data_message->getInteger("rollingCountTimeout"),
              (window_size * (timeout_step + timeout_retry_step)))
        << "window_size=" << window_size << ", timeout_step=" << timeout_step
        << ", timeout_retry_step=" << timeout_retry_step;
    EXPECT_EQ(json_data_message->getInteger("errorCount"),
              (window_size * (error_step - timeout_step)))
        << "window_size=" << window_size << ", error_step=" << error_step
        << ", timeout_step=" << timeout_step;
    uint64_t total = error_step + success_step + rejected_step + timeout_retry_step;
    EXPECT_EQ(json_data_message->getInteger("requestCount"), (window_size * total))
        << "window_size=" << window_size << ", total=" << total;

    if (total != 0) {
      EXPECT_EQ(json_data_message->getInteger("errorPercentage"),
                (static_cast<uint64_t>(100 * (static_cast<double>(total - success_step) /
                                              static_cast<double>(total)))))
          << "total=" << total << ", success_step=" << success_step;

    } else {
      EXPECT_EQ(json_data_message->getInteger("errorPercentage"), 0);
    }
  }

  std::unordered_map<std::string, std::string> buildClusterMap(absl::string_view data_message) {
    std::unordered_map<std::string, std::string> cluster_message_map;
    std::vector<std::string> messages =
        absl::StrSplit(data_message, "data: ", absl::SkipWhitespace());
    for (auto message : messages) {
      // Arrange message to remove ":" that comes from the keepalive sync.
      absl::RemoveExtraAsciiWhitespace(&message);
      std::string clear_message(absl::StripSuffix(message, ":"));
      Json::ObjectSharedPtr json_message = Json::Factory::loadFromString(clear_message);
      if (absl::StrContains(json_message->getString("type"), "HystrixCommand")) {
        std::string cluster_name(json_message->getString("name"));
        cluster_message_map[cluster_name] = message;
      }
    }
    return cluster_message_map;
  }

  TestRandomGenerator rand_;
  uint64_t window_size_ = rand_.random() % 10 + 5; // Arbitrary reasonable number.
  const std::string cluster1_name_{"test_cluster1"};
  ClusterTestInfo cluster1_{cluster1_name_};

  // Second cluster for "end and remove cluster" tests.
  const std::string cluster2_name_{"test_cluster2"};
  ClusterTestInfo cluster2_{cluster2_name_};

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Server::MockInstance> server_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;

  std::unique_ptr<HystrixSink> sink_;
  NiceMock<Stats::MockSource> source_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

TEST_F(HystrixSinkTest, EmptyFlush) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);
  sink_->flush(source_);
  std::unordered_map<std::string, std::string> cluster_message_map =
      buildClusterMap(buffer.toString());
  validateResults(cluster_message_map[cluster1_name_], 0, 0, 0, 0, 0, window_size_);
}

TEST_F(HystrixSinkTest, BasicFlow) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Only success traffic, check randomly increasing traffic
  // Later in the test we'll "shortcut" by constant traffic
  uint64_t traffic_counter = 0;

  sink_->flush(source_); // init window with 0
  for (uint64_t i = 0; i < (window_size_ - 1); i++) {
    buffer.drain(buffer.length());
    traffic_counter += rand_.random() % 1000;
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return(traffic_counter));
    sink_->flush(source_);
  }

  std::unordered_map<std::string, std::string> cluster_message_map =
      buildClusterMap(buffer.toString());

  Json::ObjectSharedPtr json_buffer =
      Json::Factory::loadFromString(cluster_message_map[cluster1_name_]);
  EXPECT_EQ(json_buffer->getInteger("rollingCountSuccess"), traffic_counter);
  EXPECT_EQ(json_buffer->getInteger("requestCount"), traffic_counter);
  EXPECT_EQ(json_buffer->getInteger("errorCount"), 0);
  EXPECT_EQ(json_buffer->getInteger("errorPercentage"), 0);

  // Check mixed traffic.
  // Values are unimportant - they represent traffic statistics, and for the purpose of the test any
  // arbitrary number will do. Only restriction is that errors >= timeouts, since in Envoy timeouts
  // are counted as errors and therefore the code that prepares the stream for the dashboard deducts
  // the number of timeouts from total number of errors.
  const uint64_t success_step = 13;
  const uint64_t error_4xx_step = 12;
  const uint64_t error_4xx_retry_step = 11;
  const uint64_t error_5xx_step = 10;
  const uint64_t error_5xx_retry_step = 9;
  const uint64_t timeout_step = 8;
  const uint64_t timeout_retry_step = 7;
  const uint64_t rejected_step = 6;

  for (uint64_t i = 0; i < (window_size_ + 1); i++) {
    buffer.drain(buffer.length());
    cluster1_.setCounterReturnValues(i, success_step, error_4xx_step, error_4xx_retry_step,
                                     error_5xx_step, error_5xx_retry_step, timeout_step,
                                     timeout_retry_step, rejected_step);
    sink_->flush(source_);
  }

  std::string rolling_map = sink_->printRollingWindows();
  EXPECT_NE(std::string::npos, rolling_map.find(cluster1_name_ + ".total"))
      << "cluster1_name = " << cluster1_name_;

  cluster_message_map = buildClusterMap(buffer.toString());

  // Check stream format and data.
  validateResults(cluster_message_map[cluster1_name_], success_step,
                  error_4xx_step + error_4xx_retry_step + error_5xx_step + error_5xx_retry_step,
                  timeout_step, timeout_retry_step, rejected_step, window_size_);

  // Check the values are reset.
  buffer.drain(buffer.length());
  sink_->resetRollingWindow();
  sink_->flush(source_);
  cluster_message_map = buildClusterMap(buffer.toString());
  validateResults(cluster_message_map[cluster1_name_], 0, 0, 0, 0, 0, window_size_);
}

//
TEST_F(HystrixSinkTest, Disconnect) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  sink_->flush(source_);
  EXPECT_EQ(buffer.length(), 0);

  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Arbitrary numbers for testing. Make sure error > timeout.
  uint64_t success_step = 1;

  for (uint64_t i = 0; i < (window_size_ + 1); i++) {
    buffer.drain(buffer.length());
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return((i + 1) * success_step));
    sink_->flush(source_);
  }

  EXPECT_NE(buffer.length(), 0);
  std::unordered_map<std::string, std::string> cluster_message_map =
      buildClusterMap(buffer.toString());
  Json::ObjectSharedPtr json_buffer =
      Json::Factory::loadFromString(cluster_message_map[cluster1_name_]);
  EXPECT_EQ(json_buffer->getInteger("rollingCountSuccess"), (success_step * window_size_));

  // Disconnect.
  buffer.drain(buffer.length());
  sink_->unregisterConnection(&callbacks_);
  sink_->flush(source_);
  EXPECT_EQ(buffer.length(), 0);

  // Reconnect.
  buffer.drain(buffer.length());
  sink_->registerConnection(&callbacks_);
  ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return(success_step));
  sink_->flush(source_);
  EXPECT_NE(buffer.length(), 0);
  cluster_message_map = buildClusterMap(buffer.toString());
  json_buffer = Json::Factory::loadFromString(cluster_message_map[cluster1_name_]);
  EXPECT_EQ(json_buffer->getInteger("rollingCountSuccess"), 0);
}

TEST_F(HystrixSinkTest, AddCluster) {
  InSequence s;
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Arbitrary values for testing. Make sure error > timeout.
  const uint64_t success_step = 6;
  const uint64_t error_step = 3;
  const uint64_t timeout_step = 1;

  const uint64_t success_step2 = 44;
  const uint64_t error_step2 = 33;
  const uint64_t timeout_step2 = 22;

  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  // Add cluster and "run" some traffic.
  std::unordered_map<std::string, std::string> cluster_message_map =
      addSecondClusterAndSendDataHelper(buffer, success_step, error_step, timeout_step,
                                        success_step2, error_step2, timeout_step2);

  // Expect that add worked.
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end())
      << "cluster1_name = " << cluster1_name_;
  ASSERT_NE(cluster_message_map.find(cluster2_name_), cluster_message_map.end())
      << "cluster2_name = " << cluster2_name_;

  // Check stream format and data.
  validateResults(cluster_message_map[cluster1_name_], success_step, error_step, timeout_step, 0, 0,
                  window_size_);
  validateResults(cluster_message_map[cluster2_name_], success_step2, error_step2, timeout_step2, 0,
                  0, window_size_);
}

TEST_F(HystrixSinkTest, AddAndRemoveClusters) {
  InSequence s;
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Arbitrary values for testing. Make sure error > timeout.
  const uint64_t success_step = 436;
  const uint64_t error_step = 547;
  const uint64_t timeout_step = 156;

  const uint64_t success_step2 = 309;
  const uint64_t error_step2 = 934;
  const uint64_t timeout_step2 = 212;

  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  // Add cluster and "run" some traffic.
  addSecondClusterAndSendDataHelper(buffer, success_step, error_step, timeout_step, success_step2,
                                    error_step2, timeout_step2);

  // Remove cluster and flush data to sink.
  removeSecondClusterHelper(buffer);

  // Check that removed worked.
  std::unordered_map<std::string, std::string> cluster_message_map =
      buildClusterMap(buffer.toString());
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end())
      << "cluster1_name = " << cluster1_name_;
  ASSERT_EQ(cluster_message_map.find(cluster2_name_), cluster_message_map.end())
      << "cluster2_name = " << cluster2_name_;

  // Add cluster again and flush data to sink.
  addSecondClusterHelper(buffer);

  sink_->flush(source_);

  // Check that add worked.
  cluster_message_map = buildClusterMap(buffer.toString());
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end())
      << "cluster1_name = " << cluster1_name_;
  ASSERT_NE(cluster_message_map.find(cluster2_name_), cluster_message_map.end())
      << "cluster2_name = " << cluster2_name_;

  // Check that old values of test_cluster2 were deleted.
  validateResults(cluster_message_map[cluster2_name_], 0, 0, 0, 0, 0, window_size_);
}
} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
