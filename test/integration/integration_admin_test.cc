#include "test/integration/integration_admin_test.h"

#include "envoy/http/header_map.h"

#include "common/json/json_loader.h"

#include "test/integration/utility.h"

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationAdminTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(IntegrationAdminTest, HealthCheck) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/healthcheck", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/healthcheck/fail", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/healthcheck/ok", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http_buffer"), "GET", "/healthcheck",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(IntegrationAdminTest, AdminLogging) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/logging", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad level
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?level=blah",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad logger
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?blah=info",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // This is going to stomp over custom log levels that are set on the command line.
  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/logging?level=warning", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ("warning", logger.levelString());
  }

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?assert=trace",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(spdlog::level::trace, Logger::Registry::getLog(Logger::Id::assert).level());

  const char* level_name = spdlog::level::level_names[default_log_level_];
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                fmt::format("/logging?level={}", level_name), "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ(level_name, logger.levelString());
  }
}

TEST_P(IntegrationAdminTest, Admin) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/server_info", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=blah",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  Json::ObjectSharedPtr statsjson = Json::Factory::loadFromString(response->body());
  EXPECT_TRUE(statsjson->hasObject("stats"));

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=prometheus", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_THAT(response->body(),
              testing::HasSubstr("http.downstream_rq{envoy.response_code_class=4xx,envoy.http_conn_"
                                 "manager_prefix=admin} 4\n"));
  EXPECT_THAT(response->body(), testing::HasSubstr("# TYPE http.downstream_rq counter\n"));
  EXPECT_THAT(response->body(),
              testing::HasSubstr("cluster.upstream_cx_active{envoy.cluster_name=cds} 0\n"));
  EXPECT_THAT(response->body(), testing::HasSubstr("# TYPE cluster.upstream_cx_active gauge\n"));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/clusters", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_THAT(response->body(), testing::HasSubstr("added_via_api"));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/hot_restart_version",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/reset_counters", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/certs", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/listeners", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  std::vector<Json::ObjectSharedPtr> listener_info = json->asObjectArray();
  auto listener_info_it = listener_info.cbegin();
  auto listeners = test_server_->server().listenerManager().listeners();
  auto listener_it = listeners.cbegin();
  for (; listener_info_it != listener_info.end() && listener_it != listeners.end();
       ++listener_info_it, ++listener_it) {
    EXPECT_EQ(listener_it->get().socket().localAddress()->asString(),
              (*listener_info_it)->asString());
  }
}

// Successful call to startProfiler requires tcmalloc.
#ifdef TCMALLOC

TEST_P(IntegrationAdminTest, AdminCpuProfilerStart) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/cpuprofiler?enable=y", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler?enable=n",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}
#endif

} // namespace Envoy
