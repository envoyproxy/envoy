#include <sstream>

#include "common/common/empty_string.h"
#include "common/http/codes.h"
#include "common/stats/hystrix.h"
#include "common/stats/stats_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

// Copied from CodeUtilityTest in codes_test.cc.
class HystrixUtilityTest : public testing::Test {
public:
  void addResponse(uint64_t code, bool canary, bool internal_request,
                   const std::string& request_vhost_name = EMPTY_STRING,
                   const std::string& request_vcluster_name = EMPTY_STRING,
                   const std::string& from_az = EMPTY_STRING,
                   const std::string& to_az = EMPTY_STRING) {
    Http::CodeUtility::ResponseStatInfo info{global_store_,
                                             cluster_scope_,
                                             "cluster.clusterName.",
                                             code,
                                             internal_request,
                                             request_vhost_name,
                                             request_vcluster_name,
                                             from_az,
                                             to_az,
                                             canary};

    Http::CodeUtility::chargeResponseStat(info);
  }

  IsolatedStoreImpl global_store_;
  IsolatedStoreImpl cluster_scope_;
};

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
  // EXPECT_EQ(actual, std::to_string(expected));
  return actual;
}

// This part is useful for testing updateRollingWindowMap()
// by using addResponse().
TEST_F(HystrixUtilityTest, CreateDataMessage) {
  Stats::Hystrix hystrix;
  std::stringstream ss;
  std::string cluster_name = "clusterName";
  uint64_t expected_queue_size = 12;
  uint64_t expectedReportingHosts = 16;

  // Insert data to rolling window.
  for (uint64_t i = 0; i < 14; i++) {
    hystrix.incCounter();
    addResponse(201, false, false);
    addResponse(401, false, false);
    addResponse(501, false, true);

    hystrix.updateRollingWindowMap(cluster_scope_, cluster_name);
  }

  hystrix.getClusterStats(ss, cluster_name, expected_queue_size, expectedReportingHosts);
  std::string dataMessage = ss.str();

  // Check stream format and data.
  EXPECT_EQ(getStreamField(dataMessage, "errorPercentage"), "66");
  EXPECT_EQ(getStreamField(dataMessage, "errorCount"), "20");
  EXPECT_EQ(getStreamField(dataMessage, "requestCount"), "30");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSuccess"), "10");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountTimeout"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "propertyValue_queueSizeRejectionThreshold"),
            std::to_string(expected_queue_size));
  EXPECT_EQ(getStreamField(dataMessage, "reportingHosts"), std::to_string(expectedReportingHosts));
}

TEST(Hystrix, CreateDataMessage) {
  Stats::Hystrix hystrix;
  std::stringstream ss;
  std::string cluster_name = "clusterName";
  uint64_t expected_queue_size = 12;
  uint64_t expectedReportingHosts = 16;

  EXPECT_EQ(hystrix.GetRollingWindowIntervalInMs(), 1000);
  EXPECT_EQ(hystrix.GetPingIntervalInMs(), 3000);

  // Insert data to rolling window.
  for (uint64_t i = 0; i < 15; i++) {
    hystrix.incCounter();
    hystrix.pushNewValue("cluster.clusterName.timeouts", (i + 1) * 3);
    hystrix.pushNewValue("cluster.clusterName.errors", (i + 1) * 17);
    hystrix.pushNewValue("cluster.clusterName.success", (i + 1) * 7);
    hystrix.pushNewValue("cluster.clusterName.rejected", (i + 1) * 8);
    hystrix.pushNewValue("cluster.clusterName.total", (i + 1) * 35);
  }
  hystrix.getClusterStats(ss, cluster_name, expected_queue_size, expectedReportingHosts);
  std::string dataMessage = ss.str();

  // Check stream format and data.
  EXPECT_EQ(getStreamField(dataMessage, "errorPercentage"), "80");
  EXPECT_EQ(getStreamField(dataMessage, "errorCount"), "170");
  EXPECT_EQ(getStreamField(dataMessage, "requestCount"), "350");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSemaphoreRejected"), "80");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSuccess"), "70");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountTimeout"), "30");
  EXPECT_EQ(getStreamField(dataMessage, "propertyValue_queueSizeRejectionThreshold"),
            std::to_string(expected_queue_size));
  EXPECT_EQ(getStreamField(dataMessage, "reportingHosts"), std::to_string(expectedReportingHosts));

  // Check reset of window.
  ss.str("");
  hystrix.resetRollingWindow();
  hystrix.getClusterStats(ss, cluster_name, expected_queue_size, expectedReportingHosts);
  dataMessage = ss.str();

  EXPECT_EQ(getStreamField(dataMessage, "errorPercentage"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "errorCount"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "requestCount"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSemaphoreRejected"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountSuccess"), "0");
  EXPECT_EQ(getStreamField(dataMessage, "rollingCountTimeout"), "0");
}

} // namespace Stats
} // namespace Envoy
