#include <sstream>

#include "common/stats/hystrix.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

void checkStreamField(std::string dataMessage, std::string key, uint64_t expected) {
  std::string actual = dataMessage.substr(dataMessage.find(key));
  actual = actual.substr(actual.find(" ") + 1);
  std::size_t length = actual.find(",");
  actual = actual.substr(0, length);
  EXPECT_EQ(actual, std::to_string(expected));
}

TEST(Hystrix, CreateDataMessage) {
  Stats::Hystrix hystrix;
  std::stringstream ss;
  std::string clusterName = "clusterName";
  uint64_t expectedQueueSize = 12;
  uint64_t expectedReportingHosts = 16;

  EXPECT_EQ(hystrix.GetRollingWindowIntervalInMs(), 1000);
  EXPECT_EQ(hystrix.GetPingIntervalInMs(), 3000);

   // insert data to rolling window
  for (uint64_t i = 0; i < 10; i++) {
    hystrix.incCounter();
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_timeout", i + 1);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_per_try_timeout", (i + 1) * 2);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_5xx", (i + 1) * 3);
    hystrix.pushNewValue("cluster.clusterName.retry.upstream_rq_5xx", (i + 1) * 4);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_4xx", (i + 1) * 5);
    hystrix.pushNewValue("cluster.clusterName.retry.upstream_rq_4xx", (i + 1) * 6);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_2xx", (i + 1) * 7);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_pending_overflow", (i + 1) * 8);
  }
  hystrix.getClusterStats(ss, clusterName, expectedQueueSize, expectedReportingHosts);
  std::string dataMessage = ss.str();

  // check stream format and data
  checkStreamField(dataMessage, "errorPercentage", 80);
  checkStreamField(dataMessage, "errorCount", 153);
  checkStreamField(dataMessage, "requestCount", 315);
  checkStreamField(dataMessage, "rollingCountSemaphoreRejected", 72);
  checkStreamField(dataMessage, "rollingCountSuccess", 63);
  checkStreamField(dataMessage, "rollingCountTimeout", 27);
  checkStreamField(dataMessage, "propertyValue_queueSizeRejectionThreshold", expectedQueueSize);
  checkStreamField(dataMessage, "reportingHosts", expectedReportingHosts);

  // make sure the rolling window really rolls
  ss.str("");
  for (uint64_t i = 10; i < 13; i++) {
    hystrix.incCounter();
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_timeout", i + 1);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_per_try_timeout", (i + 1) * 2);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_5xx", (i + 1) * 3);
    hystrix.pushNewValue("cluster.clusterName.retry.upstream_rq_5xx", (i + 1) * 4);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_4xx", (i + 1) * 5);
    hystrix.pushNewValue("cluster.clusterName.retry.upstream_rq_4xx", (i + 1) * 6);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_2xx", (i + 1) * 7);
    hystrix.pushNewValue("cluster.clusterName.upstream_rq_pending_overflow", (i + 1) * 8);
  }
  hystrix.getClusterStats(ss, clusterName, expectedQueueSize, expectedReportingHosts);
  dataMessage = ss.str();

  checkStreamField(dataMessage, "errorPercentage", 80);
  checkStreamField(dataMessage, "errorCount", 153);
  checkStreamField(dataMessage, "requestCount", 315);
  checkStreamField(dataMessage, "rollingCountSemaphoreRejected", 72);
  checkStreamField(dataMessage, "rollingCountSuccess", 63);
  checkStreamField(dataMessage, "rollingCountTimeout", 27);

  // check reset of window
  ss.str("");
  hystrix.resetRollingWindow();
  hystrix.getClusterStats(ss, clusterName, expectedQueueSize, expectedReportingHosts);
  dataMessage = ss.str();

  checkStreamField(dataMessage, "errorPercentage", 0);
  checkStreamField(dataMessage, "errorCount", 0);
  checkStreamField(dataMessage, "requestCount", 0);
  checkStreamField(dataMessage, "rollingCountSemaphoreRejected", 0);
  checkStreamField(dataMessage, "rollingCountSuccess", 0);
  checkStreamField(dataMessage, "rollingCountTimeout", 0);
}

} // namespace Stats
} // namespace Envoy
