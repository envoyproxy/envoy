#include <chrono>
#include <memory>
#include <sstream>

#include "common/stats/stats_impl.h"

#include "extensions/stat_sinks/common/hystrix/hystrix.h"

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
namespace Common {
namespace HystrixNameSpace {

class HystrixSinkTest : public testing::Test {
public:
  HystrixSinkTest() { sink_.reset(new HystrixSink(server_)); }

  absl::string_view getStreamField(absl::string_view dataMessage, absl::string_view key) {
    absl::string_view::size_type key_pos = dataMessage.find(key);
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view trimDataBeforeKey = dataMessage.substr(key_pos);
    key_pos = trimDataBeforeKey.find(" ");
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view trimDataAfterValue = trimDataBeforeKey.substr(key_pos + 1);
    key_pos = trimDataAfterValue.find(",");
    EXPECT_NE(absl::string_view::npos, key_pos);
    absl::string_view actual = trimDataAfterValue.substr(0, key_pos);
    return actual;
  }

  Buffer::OwnedImpl createClusterAndCallbacks() {

    // set cluster
    cluster_.info_->name_ = "test_cluster";
    cluster_map_.emplace("test_cluster", cluster_);
    ON_CALL(server_, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(cluster_manager_, clusters()).WillByDefault(Return(cluster_map_));

    // set callbacks to send data to buffer
    Buffer::OwnedImpl buffer;
    auto encode_callback = [&buffer](Buffer::Instance& data, bool) {
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
  NiceMock<Upstream::MockCluster> cluster_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<HystrixSink> sink_;
};

TEST_F(HystrixSinkTest, EmptyFlush) {
  InSequence s;
  Buffer::OwnedImpl buffer = createClusterAndCallbacks();
  // register callback to sink
  sink_->registerConnection(&callbacks_);

  sink_->beginFlush();
  sink_->endFlush();
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
  // register callback to sink
  sink_->registerConnection(&callbacks_);

  NiceMock<Stats::MockCounter> success_counter;
  success_counter.name_ = "cluster.test_cluster.upstream_rq_2xx";
  NiceMock<Stats::MockCounter> error_counter;
  error_counter.name_ = "cluster.test_cluster.upstream_rq_5xx";
  NiceMock<Stats::MockCounter> timeout_counter;
  timeout_counter.name_ = "cluster.test_cluster.upstream_rq_timeout";
  NiceMock<Stats::MockCounter> rejected_counter;
  rejected_counter.name_ = "cluster.test_cluster.upstream_rq_pending_overflow";

  for (int i = 0; i < 12; i++) {
    buffer.drain(buffer.length());
    ON_CALL(timeout_counter, value()).WillByDefault(Return((i + 1) * 3));
    ON_CALL(error_counter, value()).WillByDefault(Return((i + 1) * 17));
    ON_CALL(success_counter, value()).WillByDefault(Return((i + 1) * 7));
    ON_CALL(rejected_counter, value()).WillByDefault(Return((i + 1) * 8));
    sink_->beginFlush();
    sink_->flushCounter(timeout_counter, 1);
    sink_->flushCounter(error_counter, 1);
    sink_->flushCounter(success_counter, 1);
    sink_->flushCounter(rejected_counter, 1);
    sink_->endFlush();
  }

  //  //std::string rolling_map = sink_->getStats().printRollingWindow();
  std::string rolling_map = sink_->getStats().printRollingWindow();
  std::size_t pos = rolling_map.find("cluster.test_cluster.total");
  EXPECT_NE(std::string::npos, pos);
  //  //EXPECT_NE(absl::string_view::npos, map.find("cluster.test_cluster.total"));

  std::string data_message = TestUtility::bufferToString(buffer);

  // check stream format and data
  EXPECT_EQ(getStreamField(data_message, "errorCount"), "140"); // note that on regular operation,
                                                                // 5xx and timeout are raised
                                                                // together, so timeouts are reduced
                                                                // from 5xx count
  EXPECT_EQ(getStreamField(data_message, "requestCount"), "320");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "80");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "70");
  EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "30");
  EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "78");

  // check the values are reset
  buffer.drain(buffer.length());
  sink_->getStats().resetRollingWindow();
  sink_->beginFlush();
  sink_->endFlush();
  data_message = TestUtility::bufferToString(buffer);
  EXPECT_EQ(getStreamField(data_message, "errorPercentage"), "0");
  EXPECT_EQ(getStreamField(data_message, "errorCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "requestCount"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_EQ(getStreamField(data_message, "rollingCountTimeout"), "0");
}

TEST_F(HystrixSinkTest, Disconnect) {
  InSequence s;

  Buffer::OwnedImpl buffer = createClusterAndCallbacks();

  // flush with no connection
  NiceMock<Stats::MockCounter> success_counter;
  success_counter.name_ = "cluster.test_cluster.upstream_rq_2xx";
  ON_CALL(success_counter, value()).WillByDefault(Return(1234));

  sink_->beginFlush();
  sink_->flushCounter(success_counter, 1);
  sink_->endFlush();
  EXPECT_EQ(buffer.length(), 0);

  // register callback to sink
  sink_->registerConnection(&callbacks_);
  sink_->beginFlush();
  sink_->flushCounter(success_counter, 1);
  sink_->endFlush();
  std::string data_message = TestUtility::bufferToString(buffer);
  EXPECT_EQ(getStreamField(data_message, "rollingCountSuccess"), "0");
  EXPECT_NE(buffer.length(), 0);

  // disconnect
  buffer.drain(buffer.length());
  sink_->unregisterConnection();
  sink_->beginFlush();
  sink_->flushCounter(success_counter, 1);
  sink_->endFlush();
  EXPECT_EQ(buffer.length(), 0);
}

} // namespace HystrixNameSpace
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
