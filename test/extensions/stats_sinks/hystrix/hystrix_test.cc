#include <chrono>
#include <memory>
#include <sstream>

#include "source/common/json/json_loader.h"
#include "source/extensions/stat_sinks/hystrix/hystrix.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/priority_set.h"

#include "absl/strings/str_split.h"
#include "circllhist.h"
#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {
namespace {

class ClusterTestInfo {

public:
  ClusterTestInfo(const std::string cluster_name) : cluster_name_(cluster_name) {
    ON_CALL(cluster_, info()).WillByDefault(Return(cluster_info_ptr_));
    ON_CALL(*cluster_info_, name()).WillByDefault(testing::ReturnRefOfCopy(cluster_name_));
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(cluster_stats_scope_));

    // Set gauge value.
    membership_total_gauge_.name_ = "membership_total";
    ON_CALL(cluster_stats_scope_, gauge("membership_total", Stats::Gauge::ImportMode::NeverImport))
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

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster_;
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

class HistogramWrapper {
public:
  HistogramWrapper() : histogram_(hist_alloc()) {}

  ~HistogramWrapper() { hist_free(histogram_); }

  const histogram_t* getHistogram() { return histogram_; }

  void setHistogramValues(const std::vector<uint64_t>& values) {
    for (uint64_t value : values) {
      hist_insert_intscale(histogram_, value, 0, 1);
    }
  }

private:
  histogram_t* histogram_;
};

class HystrixSinkTest : public testing::Test {
public:
  HystrixSinkTest() { sink_ = std::make_unique<HystrixSink>(server_, window_size_); }

  void createClusterAndCallbacks() {
    // Set cluster.
    cluster_maps_.active_clusters_.emplace(cluster1_name_, cluster1_.cluster_);
    ON_CALL(server_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_maps_));

    ON_CALL(callbacks_, encodeData(_, _)).WillByDefault(Invoke([&](Buffer::Instance& data, bool) {
      // Set callbacks to send data to buffer. This will append to the end of the buffer, so
      // multiple calls will all be dumped one after another into this buffer.
      cluster_stats_buffer_.add(data);
    }));
  }

  void addClusterToMap(const std::string& cluster_name,
                       NiceMock<Upstream::MockClusterMockPrioritySet>& cluster) {
    cluster_maps_.active_clusters_.emplace(cluster_name, cluster);
    // Redefining since cluster_maps_ is returned by value.
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_maps_));
  }

  void removeClusterFromMap(const std::string& cluster_name) {
    cluster_maps_.active_clusters_.erase(cluster_name);
    // Redefining since cluster_maps_ is returned by value.
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_maps_));
  }

  void addSecondClusterHelper(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    cluster2_.setCountersToZero();
    addClusterToMap(cluster2_name_, cluster2_.cluster_);
  }

  absl::node_hash_map<std::string, std::string>
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
      sink_->flush(snapshot_);
    }

    return buildClusterMap(buffer.toString());
  }

  void removeSecondClusterHelper(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    removeClusterFromMap(cluster2_name_);
    sink_->flush(snapshot_);
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

  absl::node_hash_map<std::string, std::string> buildClusterMap(absl::string_view data_message) {
    absl::node_hash_map<std::string, std::string> cluster_message_map;
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
  NiceMock<Server::Configuration::MockServerFactoryContext> server_;
  Upstream::ClusterManager::ClusterInfoMaps cluster_maps_;
  Buffer::OwnedImpl cluster_stats_buffer_;

  std::unique_ptr<HystrixSink> sink_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

TEST_F(HystrixSinkTest, EmptyFlush) {
  InSequence s;
  createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);
  sink_->flush(snapshot_);
  absl::node_hash_map<std::string, std::string> cluster_message_map =
      buildClusterMap(cluster_stats_buffer_.toString());
  validateResults(cluster_message_map[cluster1_name_], 0, 0, 0, 0, 0, window_size_);
}

TEST_F(HystrixSinkTest, BasicFlow) {
  InSequence s;
  createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Only success traffic, check randomly increasing traffic
  // Later in the test we'll "shortcut" by constant traffic
  uint64_t traffic_counter = 0;

  sink_->flush(snapshot_); // init window with 0
  for (uint64_t i = 0; i < (window_size_ - 1); i++) {
    cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
    traffic_counter += rand_.random() % 1000;
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return(traffic_counter));
    sink_->flush(snapshot_);
  }

  absl::node_hash_map<std::string, std::string> cluster_message_map =
      buildClusterMap(cluster_stats_buffer_.toString());

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
    cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
    cluster1_.setCounterReturnValues(i, success_step, error_4xx_step, error_4xx_retry_step,
                                     error_5xx_step, error_5xx_retry_step, timeout_step,
                                     timeout_retry_step, rejected_step);
    sink_->flush(snapshot_);
  }

  std::string rolling_map = sink_->printRollingWindows();
  EXPECT_NE(std::string::npos, rolling_map.find(cluster1_name_ + ".total"))
      << "cluster1_name = " << cluster1_name_;

  cluster_message_map = buildClusterMap(cluster_stats_buffer_.toString());

  // Check stream format and data.
  validateResults(cluster_message_map[cluster1_name_], success_step,
                  error_4xx_step + error_4xx_retry_step + error_5xx_step + error_5xx_retry_step,
                  timeout_step, timeout_retry_step, rejected_step, window_size_);

  // Check the values are reset.
  cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
  sink_->resetRollingWindow();
  sink_->flush(snapshot_);
  cluster_message_map = buildClusterMap(cluster_stats_buffer_.toString());
  validateResults(cluster_message_map[cluster1_name_], 0, 0, 0, 0, 0, window_size_);
}

//
TEST_F(HystrixSinkTest, Disconnect) {
  InSequence s;
  createClusterAndCallbacks();

  sink_->flush(snapshot_);
  EXPECT_EQ(cluster_stats_buffer_.length(), 0);

  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // Arbitrary numbers for testing. Make sure error > timeout.
  uint64_t success_step = 1;

  for (uint64_t i = 0; i < (window_size_ + 1); i++) {
    cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
    ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return((i + 1) * success_step));
    sink_->flush(snapshot_);
  }

  EXPECT_NE(cluster_stats_buffer_.length(), 0);
  absl::node_hash_map<std::string, std::string> cluster_message_map =
      buildClusterMap(cluster_stats_buffer_.toString());
  Json::ObjectSharedPtr json_buffer =
      Json::Factory::loadFromString(cluster_message_map[cluster1_name_]);
  EXPECT_EQ(json_buffer->getInteger("rollingCountSuccess"), (success_step * window_size_));

  // Disconnect.
  cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
  sink_->unregisterConnection(&callbacks_);
  sink_->flush(snapshot_);
  EXPECT_EQ(cluster_stats_buffer_.length(), 0);

  // Reconnect.
  cluster_stats_buffer_.drain(cluster_stats_buffer_.length());
  sink_->registerConnection(&callbacks_);
  ON_CALL(cluster1_.success_counter_, value()).WillByDefault(Return(success_step));
  sink_->flush(snapshot_);
  EXPECT_NE(cluster_stats_buffer_.length(), 0);
  cluster_message_map = buildClusterMap(cluster_stats_buffer_.toString());
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

  createClusterAndCallbacks();

  // Add cluster and "run" some traffic.
  absl::node_hash_map<std::string, std::string> cluster_message_map =
      addSecondClusterAndSendDataHelper(cluster_stats_buffer_, success_step, error_step,
                                        timeout_step, success_step2, error_step2, timeout_step2);

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

  createClusterAndCallbacks();

  // Add cluster and "run" some traffic.
  addSecondClusterAndSendDataHelper(cluster_stats_buffer_, success_step, error_step, timeout_step,
                                    success_step2, error_step2, timeout_step2);

  // Remove cluster and flush data to sink.
  removeSecondClusterHelper(cluster_stats_buffer_);

  // Check that removed worked.
  absl::node_hash_map<std::string, std::string> cluster_message_map =
      buildClusterMap(cluster_stats_buffer_.toString());
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end())
      << "cluster1_name = " << cluster1_name_;
  ASSERT_EQ(cluster_message_map.find(cluster2_name_), cluster_message_map.end())
      << "cluster2_name = " << cluster2_name_;

  // Add cluster again and flush data to sink.
  addSecondClusterHelper(cluster_stats_buffer_);

  sink_->flush(snapshot_);

  // Check that add worked.
  cluster_message_map = buildClusterMap(cluster_stats_buffer_.toString());
  ASSERT_NE(cluster_message_map.find(cluster1_name_), cluster_message_map.end())
      << "cluster1_name = " << cluster1_name_;
  ASSERT_NE(cluster_message_map.find(cluster2_name_), cluster_message_map.end())
      << "cluster2_name = " << cluster2_name_;

  // Check that old values of test_cluster2 were deleted.
  validateResults(cluster_message_map[cluster2_name_], 0, 0, 0, 0, 0, window_size_);
}

TEST_F(HystrixSinkTest, HistogramTest) {
  InSequence s;

  // Create histogram for the Hystrix sink to read.
  auto histogram = std::make_shared<NiceMock<Stats::MockParentHistogram>>();
  histogram->name_ = "cluster." + cluster1_name_ + ".upstream_rq_time";
  histogram->setTagExtractedName("cluster.upstream_rq_time");
  histogram->addTag(Stats::Tag{Config::TagNames::get().CLUSTER_NAME, cluster1_name_});
  histogram->used_ = true;

  // Init with data such that the quantile value is equal to the quantile.
  std::vector<uint64_t> h1_interval_values;
  for (size_t i = 0; i < 100; ++i) {
    h1_interval_values.push_back(i);
  }

  HistogramWrapper hist1_interval;
  hist1_interval.setHistogramValues(h1_interval_values);

  Stats::HistogramStatisticsImpl h1_interval_statistics(hist1_interval.getHistogram());
  ON_CALL(*histogram, intervalStatistics())
      .WillByDefault(testing::ReturnRef(h1_interval_statistics));
  snapshot_.histograms_.push_back(*histogram);

  createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);
  sink_->flush(snapshot_);

  absl::node_hash_map<std::string, std::string> cluster_message_map =
      buildClusterMap(cluster_stats_buffer_.toString());

  Json::ObjectSharedPtr latency = Json::Factory::loadFromString(cluster_message_map[cluster1_name_])
                                      ->getObject("latencyExecute");

  // Data was added such that the value equals the quantile:
  // "latencyExecute": {"99.5": 99.500000, "95": 95.000000, "90": 90.000000, "100": 100.000000, "0":
  // 0.000000, "25": 25.000000, "99": 99.000000, "50": 50.000000, "75": 75.000000}.
  for (const double quantile : hystrix_quantiles) {
    EXPECT_EQ(quantile * 100, latency->getDouble(fmt::sprintf("%g", quantile * 100)));
  }
}

TEST_F(HystrixSinkTest, HystrixEventStreamHandler) {
  InSequence s;
  createClusterAndCallbacks();
  // Register callback to sink.
  sink_->registerConnection(&callbacks_);

  // This value doesn't matter in handlerHystrixEventStream
  absl::string_view path_and_query;

  Http::TestResponseHeaderMapImpl response_headers;

  NiceMock<Server::MockAdminStream> admin_stream_mock;
  NiceMock<Network::MockConnection> connection_mock;

  auto addr_instance_ = Envoy::Network::Utility::parseInternetAddress("2.3.4.5", 123, false);

  Http::MockHttp1StreamEncoderOptions stream_encoder_options;
  ON_CALL(admin_stream_mock, getDecoderFilterCallbacks()).WillByDefault(ReturnRef(callbacks_));
  ON_CALL(admin_stream_mock, http1StreamEncoderOptions())
      .WillByDefault(Return(Http::Http1StreamEncoderOptionsOptRef(stream_encoder_options)));
  ON_CALL(callbacks_, connection()).WillByDefault(Return(&connection_mock));
  connection_mock.stream_info_.downstream_address_provider_->setRemoteAddress(addr_instance_);

  EXPECT_CALL(stream_encoder_options, disableChunkEncoding());
  ASSERT_EQ(sink_->handlerHystrixEventStream(path_and_query, response_headers,
                                             cluster_stats_buffer_, admin_stream_mock),
            Http::Code::OK);

  // Check that response_headers has been set correctly
  EXPECT_EQ(response_headers.ContentType()->value(), "text/event-stream");
  EXPECT_EQ(response_headers.get_("cache-control"), "no-cache");
  EXPECT_EQ(response_headers.Connection()->value(), "close");
  EXPECT_EQ(response_headers.get_("access-control-allow-origin"), "*");
  EXPECT_THAT(response_headers.get_("access-control-allow-headers"), HasSubstr("Accept"));
}

} // namespace
} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
