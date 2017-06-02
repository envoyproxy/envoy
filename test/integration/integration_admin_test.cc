#include "test/integration/integration_admin_test.h"

#include "envoy/http/header_map.h"

#include "common/json/json_loader.h"

#include "test/integration/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationAdminTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(IntegrationAdminTest, HealthCheck) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/healthcheck", "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/healthcheck/fail", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/healthcheck/ok", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http_buffer"), "GET", "/healthcheck",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(IntegrationAdminTest, AdminLogging) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/logging", "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad level
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?level=blah",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad logger
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?blah=info",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // This is going to stomp over custom log levels that are set on the command line.
  response =
      IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?level=warning", "",
                                         Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ("warning", logger.levelString());
  }

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/logging?assert=trace",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(spdlog::level::trace, Logger::Registry::getLog(Logger::Id::assert).level());

  const char* level_name = spdlog::level::level_names[default_log_level_];
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                fmt::format("/logging?level={}", level_name), "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ(level_name, logger.levelString());
  }
}

TEST_P(IntegrationAdminTest, Admin) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/", "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/server_info", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/clusters", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/hot_restart_version",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/reset_counters", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/certs", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/listeners", "",
                                                Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  std::vector<Json::ObjectSharedPtr> listener_info = json->asObjectArray();
  for (std::size_t index = 0; index < listener_info.size(); index++) {
    EXPECT_EQ(test_server_->server().getListenSocketByIndex(index)->localAddress()->asString(),
              listener_info[index]->asString());
  }
}

// Successful call to startProfiler requires tcmalloc.
#ifdef TCMALLOC

TEST_P(IntegrationAdminTest, AdminCpuProfilerStart) {
  BufferingStreamDecoderPtr response =
      IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler?enable=y", "",
                                         Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler?enable=n",
                                                "", Http::CodecClient::Type::HTTP1, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}
#endif
} // Envoy
