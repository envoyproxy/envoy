#include <chrono>
#include <memory>
#include <sstream>

#include "common/stats/stats_impl.h"

#include "extensions/stat_sinks/hystrix/hystrix.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

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

  Buffer::OwnedImpl createClusterAndCallbacks() {

    // Set cluster.
    cluster_map_.emplace("test_cluster1", cluster1_);
    ON_CALL(server_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
    ON_CALL(cluster1_, info()).WillByDefault(Return(cluster1_info_ptr_));

    ON_CALL(*cluster1_info_, name()).WillByDefault(testing::ReturnRefOfCopy(cluster1_name_));
    ON_CALL(*cluster1_info_, statsScope()).WillByDefault(ReturnRef(cluster1_stats_scope_));

    // Set gauge value.
    membership_total_gauge_.name_ = "membership_total";
    ON_CALL(cluster1_stats_scope_, gauge("membership_total"))
        .WillByDefault(ReturnRef(membership_total_gauge_));
    ON_CALL(membership_total_gauge_, value()).WillByDefault(Return(5));

    // Attach counters.
    success_counter_.name_ = "upstream_rq_2xx";
    ON_CALL(cluster1_stats_scope_, counter("upstream_rq_2xx"))
        .WillByDefault(ReturnRef(success_counter_));
    error_5xx_counter_.name_ = "upstream_rq_5xx";
    ON_CALL(cluster1_stats_scope_, counter("upstream_rq_5xx"))
        .WillByDefault(ReturnRef(error_5xx_counter_));
    retry_5xx_counter_.name_ = "retry.upstream_rq_5xx";
    ON_CALL(cluster1_stats_scope_, counter("retry.upstream_rq_5xx"))
        .WillByDefault(ReturnRef(retry_5xx_counter_));
    error_4xx_counter_.name_ = "upstream_rq_4xx";
    ON_CALL(cluster1_stats_scope_, counter("upstream_rq_4xx"))
        .WillByDefault(ReturnRef(error_4xx_counter_));
    retry_4xx_counter_.name_ = "retry.upstream_rq_4xx";
    ON_CALL(cluster1_stats_scope_, counter("tetry.upstream_rq_4xx"))
        .WillByDefault(ReturnRef(retry_4xx_counter_));
    ON_CALL(retry_5xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(error_4xx_counter_, value()).WillByDefault(Return(0));
    ON_CALL(retry_5xx_counter_, value()).WillByDefault(Return(0));

    Buffer::OwnedImpl buffer;
    auto encode_callback = [&buffer](Buffer::Instance& data, bool) {
      // Set callbacks to send data to buffer.
      buffer.add(
          data); // This will append to the end of the buffer, so multiple calls will all be dumped
      // one after another into this buffer. See Buffer::Instance for other buffer
      // buffer modification options.
    };
    ON_CALL(callbacks_, encodeData(_, _)).WillByDefault(Invoke(encode_callback));

    return buffer;
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Server::MockInstance> server_;
  NiceMock<Upstream::MockCluster> cluster1_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;

  std::unique_ptr<HystrixSink> sink_;
  NiceMock<Stats::MockSource> source_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;

  Upstream::MockClusterInfo* cluster1_info_ = new NiceMock<Upstream::MockClusterInfo>();
  Upstream::ClusterInfoConstSharedPtr cluster1_info_ptr_{cluster1_info_};

  NiceMock<Stats::MockStore> stats_store_1_;
  NiceMock<Stats::MockStore> cluster1_stats_scope_;
  const std::string cluster1_name_{"test_cluster1"};

  NiceMock<Stats::MockGauge> membership_total_gauge_;
  NiceMock<Stats::MockCounter> success_counter_;
  NiceMock<Stats::MockCounter> error_5xx_counter_;
  NiceMock<Stats::MockCounter> retry_5xx_counter_;
  NiceMock<Stats::MockCounter> error_4xx_counter_;
  NiceMock<Stats::MockCounter> retry_4xx_counter_;
};

TEST_F(HystrixSinkTest, EmptyFlush) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  sink_->flush(source_);

  std::string data_message = TestUtility::bufferToString(buffer);
  EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "0");
  EXPECT_EQ(getStreamField(data_message, "errorCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "requestCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "0");
}

TEST_F(HystrixSinkTest, BasicFlow) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  for (int i = 0; i < 12; i++) {
    buffer.drain(buffer.length());
    ON_CALL(error_5xx_counter_, value()).WillByDefault(Return((i + 1) * 17));
    ON_CALL(success_counter_, value()).WillByDefault(Return((i + 1) * 7));
    cluster1_info_->stats().upstream_rq_timeout_.add(3);
    cluster1_info_->stats().upstream_rq_pending_overflow_.add(8);
    sink_->flush(source_);
  }

  std::string rolling_map = sink_->printRollingWindows();
  EXPECT_NE(std::string::npos, rolling_map.find("test_cluster1.total"));

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
  EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "0");
  EXPECT_EQ(getStreamField(data_message, "errorCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "requestCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "0");
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
  NiceMock<Upstream::MockCluster> cluster2;
  Upstream::MockClusterInfo* cluster2_info = new NiceMock<Upstream::MockClusterInfo>();
  Upstream::ClusterInfoConstSharedPtr cluster2_info_ptr{cluster2_info};
  NiceMock<Stats::MockStore> stats_store_2;
  NiceMock<Stats::MockStore> cluster2_stats_scope;
  const std::string cluster2_name{"test_cluster2"};

  ON_CALL(cluster2, info()).WillByDefault(Return(cluster2_info_ptr));

  ON_CALL(*cluster2_info, name()).WillByDefault(testing::ReturnRefOfCopy(cluster2_name));
  ON_CALL(*cluster2_info, statsScope()).WillByDefault(ReturnRef(cluster2_stats_scope));

  // Set gauge value.
  ON_CALL(cluster2_stats_scope, gauge("membership_total"))
      .WillByDefault(ReturnRef(membership_total_gauge_));

  // Attach counters.
  NiceMock<Stats::MockCounter> success_counter2;
  NiceMock<Stats::MockCounter> error_5xx_counter2;
  NiceMock<Stats::MockCounter> retry_5xx_counter2;
  NiceMock<Stats::MockCounter> error_4xx_counter2;
  NiceMock<Stats::MockCounter> retry_4xx_counter2;
  success_counter2.name_ = "upstream_rq_2xx";
  ON_CALL(cluster2_stats_scope, counter("upstream_rq_2xx"))
      .WillByDefault(ReturnRef(success_counter2));
  error_5xx_counter2.name_ = "upstream_rq_5xx";
  ON_CALL(cluster2_stats_scope, counter("upstream_rq_5xx"))
      .WillByDefault(ReturnRef(error_5xx_counter2));
  retry_5xx_counter2.name_ = "retry.upstream_rq_5xx";
  ON_CALL(cluster2_stats_scope, counter("retry.upstream_rq_5xx"))
      .WillByDefault(ReturnRef(retry_5xx_counter2));
  error_4xx_counter2.name_ = "upstream_rq_4xx";
  ON_CALL(cluster2_stats_scope, counter("upstream_rq_4xx"))
      .WillByDefault(ReturnRef(error_4xx_counter2));
  retry_4xx_counter2.name_ = "retry.upstream_rq_4xx";
  ON_CALL(cluster2_stats_scope, counter("retry.upstream_rq_4xx"))
      .WillByDefault(ReturnRef(retry_4xx_counter2));

  cluster2_info->stats().upstream_rq_timeout_.reset();
  cluster2_info->stats().upstream_rq_per_try_timeout_.reset();
  cluster2_info->stats().upstream_rq_pending_overflow_.reset();

  // Add cluster.
  cluster_map_.emplace(cluster2_name, cluster2);
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  // Generate data to both clusters.
  for (int i = 0; i < 12; i++) {
    buffer.drain(buffer.length());
    // Cluster 1
    ON_CALL(error_5xx_counter_, value()).WillByDefault(Return((i + 1) * 17));
    ON_CALL(success_counter_, value()).WillByDefault(Return((i + 1) * 7));
    cluster1_info_->stats().upstream_rq_timeout_.add(1);
    cluster1_info_->stats().upstream_rq_per_try_timeout_.add(2);
    cluster1_info_->stats().upstream_rq_pending_overflow_.add(8);

    // Cluster 2
    ON_CALL(error_5xx_counter2, value()).WillByDefault(Return((i + 1) * 1));
    ON_CALL(retry_5xx_counter2, value()).WillByDefault(Return((i + 1) * 2));
    ON_CALL(error_4xx_counter2, value()).WillByDefault(Return((i + 1) * 3));
    ON_CALL(retry_4xx_counter2, value()).WillByDefault(Return((i + 1) * 4));
    ON_CALL(success_counter2, value()).WillByDefault(Return((i + 1) * 3));
    cluster2_info->stats().upstream_rq_timeout_.add(3);
    cluster2_info->stats().upstream_rq_per_try_timeout_.add(1);
    cluster2_info->stats().upstream_rq_pending_overflow_.add(5);

    sink_->flush(source_);
  }

  std::string data_message = TestUtility::bufferToString(buffer);

  std::size_t pos1 = data_message.find(cluster1_name_);
  std::size_t pos2 = data_message.find(cluster2_name);
  ASSERT_NE(std::string::npos, pos1);
  ASSERT_NE(std::string::npos, pos2);

  std::string data_message_1 =
      (pos1 < pos2) ? data_message.substr(pos1, pos2) : data_message.substr(pos1);
  std::string data_message_2 =
      (pos2 < pos1) ? data_message.substr(pos2, pos1) : data_message.substr(pos2);

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
  ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
  sink_->flush(source_);
  data_message = TestUtility::bufferToString(buffer);

  EXPECT_NE(std::string::npos, data_message.find(cluster1_name_));
  EXPECT_EQ(std::string::npos, data_message.find(cluster2_name));

  // Add cluster again.
  buffer.drain(buffer.length());
  cluster_map_.emplace(cluster2_name, cluster2);
  ON_CALL(error_5xx_counter2, value()).WillByDefault(Return(0));
  ON_CALL(retry_5xx_counter2, value()).WillByDefault(Return(0));
  ON_CALL(error_4xx_counter2, value()).WillByDefault(Return(0));
  ON_CALL(retry_4xx_counter2, value()).WillByDefault(Return(0));
  ON_CALL(success_counter2, value()).WillByDefault(Return(0));
  cluster2_info->stats().upstream_rq_timeout_.reset();
  cluster2_info->stats().upstream_rq_per_try_timeout_.reset();
  cluster2_info->stats().upstream_rq_pending_overflow_.reset();
  ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));
  sink_->flush(source_);

  data_message = TestUtility::bufferToString(buffer);
  pos1 = data_message.find(cluster1_name_);
  pos2 = data_message.find(cluster2_name);
  ASSERT_NE(std::string::npos, pos1);
  ASSERT_NE(std::string::npos, pos2);

  data_message_2 = (pos2 < pos1) ? data_message.substr(pos2, pos1) : data_message.substr(pos2);

  // Check that old values of test_cluster2 were deleted.
  EXPECT_EQ(getStreamField(data_message_2, "errorPercentage"), "0");
  EXPECT_EQ(getStreamField(data_message_2, "errorCount"), "0");
  EXPECT_EQ(getStreamField(data_message_2, "requestCount"), "0");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountSuccess"), "0");
  EXPECT_EQ(getStreamField(data_message_2, "rollingCountTimeout"), "0");
}
} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
