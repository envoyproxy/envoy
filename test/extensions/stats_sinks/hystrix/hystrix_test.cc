#include <chrono>
#include <memory>
#include <sstream>

#include "common/stats/stats_impl.h"

#include "extensions/stat_sinks/hystrix/hystrix.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

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
  HystrixSinkTest() { sink_.reset(new HystrixSink(server_, 10)); }

  absl::string_view getStreamField(absl::string_view data_message, absl::string_view key) {
    absl::string_view::size_type key_pos = data_message.find(key);
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view trim_data_before_Key = data_message.substr(key_pos);
    key_pos = trim_data_before_Key.find(" ");
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view trim_data_after_value = trim_data_before_Key.substr(key_pos + 1);
    key_pos = trim_data_after_value.find(",");
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view actual = trim_data_after_value.substr(0, key_pos);
    return actual;
  }

  absl::string_view getStringStreamField(absl::string_view data_message, absl::string_view key) {
    return absl::StrReplaceAll(getStreamField(data_message, key), {{"\"", ""}});
  }

  Buffer::OwnedImpl createClusterAndCallbacks() {
    // Set cluster.
    cluster_map_.emplace(cluster1_name_, cluster1_.cluster_);
    ON_CALL(server_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));

    Buffer::OwnedImpl buffer;
    auto encode_callback = [&buffer](Buffer::Instance& data, bool) {
      // Set callbacks to send data to buffer.
      buffer.add(
          data); // This will append to the end of the buffer, so multiple calls will all be dumped
                 // one after another into this buffer.
    };
    ON_CALL(callbacks_, encodeData(_, _)).WillByDefault(Invoke(encode_callback));
    return buffer;
  }

  void validateAllZero(std::string data_message) {
    EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "0");
    EXPECT_EQ(getStreamField(data_message, "errorCount"), "0");
    EXPECT_EQ(getStreamField(data_message, "requestCount"), "0");
    EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "0");
    EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
    EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "0");
  }

  std::unordered_map<std::string, std::string> buildClusterMap(absl::string_view data_message) {
    std::unordered_map<std::string, std::string> cluster_message_map;
    std::vector<std::string> messages =
        absl::StrSplit(data_message, "data:", absl::SkipWhitespace());
    for (auto message : messages) {
      if (absl::StrContains(getStreamField(message, "type"), "HystrixCommand")) {
        std::string cluster_name(getStringStreamField(message, "name"));
        cluster_message_map[cluster_name] = message;
      }
    }
    return cluster_message_map;
  }

  const std::string cluster1_name_{"test_cluster1"};
  ClusterTestInfo cluster1_{cluster1_name_};

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
  std::string data_message = TestUtility::bufferToString(buffer);
  validateAllZero(data_message);
}

TEST_F(HystrixSinkTest, BasicFlow) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  for (int i = 0; i < 12; i++) {
    buffer.drain(buffer.length());
    ON_CALL(cluster1_.error_5xx_counter_, value()).WillByDefault(Return((i + 1) * 17));
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return((i + 1) * 7));
    cluster1_.cluster_info_->stats().upstream_rq_timeout_.add(3);
    cluster1_.cluster_info_->stats().upstream_rq_pending_overflow_.add(8);
    sink_->flush(source_);
  }

  std::string rolling_map = sink_->printRollingWindows();
  EXPECT_NE(std::string::npos, rolling_map.find(cluster1_name_ + ".total"));

  std::string data_message = TestUtility::bufferToString(buffer);

  // Check stream format and data.
  EXPECT_EQ(getStreamField(data_message, "errorCount"), "140"); // Note that on regular operation,
                                                                // 5xx and timeout are raised
                                                                // together, so timeouts are reduced
                                                                // from 5xx count
  EXPECT_EQ(getStreamField(data_message, "requestCount"), "320");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "80");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "70");
  EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "30");
  EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "78");

  // Check the values are reset.
  buffer.drain(buffer.length());
  sink_->resetRollingWindow();
  sink_->flush(source_);
  data_message = TestUtility::bufferToString(buffer);
  validateAllZero(data_message);
}

//
TEST_F(HystrixSinkTest, Disconnect) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  sink_->flush(source_);
  EXPECT_EQ(buffer.length(), 0);

  // Register callback to sink.
  sink_->registerConnection(&callbacks_);
  sink_->flush(source_);
  std::string data_message = TestUtility::bufferToString(buffer);
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_NE(buffer.length(), 0);

  // Disconnect.
  buffer.drain(buffer.length());
  sink_->unregisterConnection(&callbacks_);
  sink_->flush(source_);
  EXPECT_EQ(buffer.length(), 0);

  // Reconnect.
  buffer.drain(buffer.length());
  sink_->registerConnection(&callbacks_);
  sink_->flush(source_);
  data_message = TestUtility::bufferToString(buffer);
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_NE(buffer.length(), 0);
}

TEST_F(HystrixSinkTest, AddAndRemoveClusters) {
  InSequence s;

  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // New cluster.
  const std::string cluster2_name{"test_cluster2"};
  ClusterTestInfo cluster2(cluster2_name);

  // Add cluster.
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  cluster_map_.emplace(cluster2_name, cluster2.cluster_);
  // Redefining since cluster_map_ is returned by value.
  ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));

  // Generate data to both clusters.
  sink_->flush(source_);
  for (int i = 0; i < 12; i++) {
    buffer.drain(buffer.length());
    // Cluster 1
    ON_CALL(cluster1_.error_5xx_counter_, value()).WillByDefault(Return((i + 1) * 17));
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return((i + 1) * 7));
    cluster1_.cluster_info_->stats().upstream_rq_timeout_.add(1);
    cluster1_.cluster_info_->stats().upstream_rq_per_try_timeout_.add(2);
    cluster1_.cluster_info_->stats().upstream_rq_pending_overflow_.add(8);

    // Cluster 2
    ON_CALL(cluster2.error_5xx_counter_, value()).WillByDefault(Return((i + 1) * 1));
    ON_CALL(cluster2.retry_5xx_counter_, value()).WillByDefault(Return((i + 1) * 2));
    ON_CALL(cluster2.error_4xx_counter_, value()).WillByDefault(Return((i + 1) * 3));
    ON_CALL(cluster2.retry_4xx_counter_, value()).WillByDefault(Return((i + 1) * 4));
    ON_CALL(cluster2.success_counter_, value()).WillByDefault(Return((i + 1) * 3));
    cluster2.cluster_info_->stats().upstream_rq_timeout_.add(3);
    cluster2.cluster_info_->stats().upstream_rq_per_try_timeout_.add(1);
    cluster2.cluster_info_->stats().upstream_rq_pending_overflow_.add(5);

    sink_->flush(source_);
  }

  std::unordered_map<std::string, std::string> cluster_message_map =
      buildClusterMap(TestUtility::bufferToString(buffer));
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end());
  ASSERT_NE(cluster_message_map.find(cluster2_name), cluster_message_map.end());

  std::string data_message_1 = cluster_message_map[cluster1_name_];
  std::string data_message_2 = cluster_message_map[cluster2_name];

  // Check stream format and data.
  EXPECT_EQ(getStreamField(data_message_1, "errorCount"), "160"); // Note that on regular operation,
  // 5xx and timeout are raised together, so timeouts are reduced from 5xx count.
  EXPECT_EQ(getStreamField(data_message_1, "requestCount"), "340");
  EXPECT_EQ(getStreamField(data_message_1, "rollingCountSemaphoreRejected"), "80");
  EXPECT_EQ(getStreamField(data_message_1, "rollingCountSuccess"), "70");
  EXPECT_EQ(getStreamField(data_message_1, "rollingCountTimeout"), "30");
  EXPECT_EQ(getStreamField(data_message_1, "errorPercentage"), "79");

  // Check stream format and data.
  EXPECT_EQ(getStreamField(data_message_2, "errorCount"), "70"); // note that on regular operation,
                                                                 // 5xx and timeout are raised
                                                                 // together, so timeouts are
                                                                 // reduced from 5xx count
  EXPECT_EQ(getStreamField(data_message_2, "requestCount"), "190");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountSemaphoreRejected"), "50");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountSuccess"), "30");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountTimeout"), "40");
  EXPECT_EQ(getStreamField(data_message_2, "errorPercentage"), "84");

  buffer.drain(buffer.length());

  // Removing cluster.
  cluster_map_.erase(cluster2_name);
  // Redefining since cluster_map_ is returned by value.
  ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
  sink_->flush(source_);

  cluster_message_map.clear();
  cluster_message_map = buildClusterMap(TestUtility::bufferToString(buffer));
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end());
  ASSERT_EQ(cluster_message_map.find(cluster2_name), cluster_message_map.end());

  // Add cluster again.
  buffer.drain(buffer.length());
  cluster2.setCountersToZero();
  cluster_map_.emplace(cluster2_name, cluster2.cluster_);
  // Redefining since cluster_map_ is returned by value.
  ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));

  cluster2.cluster_info_->stats().upstream_rq_timeout_.reset();
  cluster2.cluster_info_->stats().upstream_rq_per_try_timeout_.reset();
  cluster2.cluster_info_->stats().upstream_rq_pending_overflow_.reset();
  sink_->flush(source_);

  cluster_message_map.clear();
  cluster_message_map = buildClusterMap(TestUtility::bufferToString(buffer));
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end());
  ASSERT_NE(cluster_message_map.find(cluster2_name), cluster_message_map.end());

  // Check that old values of test_cluster2 were deleted.
  validateAllZero(cluster_message_map[cluster2_name]);
}
} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
