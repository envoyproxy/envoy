#include "integration.h"
#include "utility.h"

#include "envoy/http/header_map.h"

TEST_F(IntegrationTest, HealthCheck) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      HTTP_PORT, "GET", "/healthcheck", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/healthcheck/fail", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(HTTP_PORT, "GET", "/healthcheck", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("503", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/healthcheck/ok", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(HTTP_PORT, "GET", "/healthcheck", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(HTTP_BUFFER_PORT, "GET", "/healthcheck", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_F(IntegrationTest, AdminLogging) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      ADMIN_PORT, "GET", "/logging", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad level
  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/logging?level=blah", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // Bad logger
  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/logging?blah=info", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  // This is going to stomp over custom log levels that are set on the command line.
  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/logging?level=warning", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ("warning", logger.levelString());
  }

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/logging?assert=trace", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_EQ(spdlog::level::trace, Logger::Registry::getLog(Logger::Id::assert).level());

  const char* level_name = spdlog::level::level_names[default_log_level_];
  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET",
                                                fmt::format("/logging?level={}", level_name), "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ(level_name, logger.levelString());
  }
}

TEST_F(IntegrationTest, Admin) {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      ADMIN_PORT, "GET", "/", "", Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/server_info", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/stats", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/clusters", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/cpuprofiler", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());

  // TO DO: Change to expect 200 when profiler path is a configurable parameter
  // via the Envoy config JSON. Currently call returns 400 due to inaccessable profiler path
  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/cpuprofiler?enable=y", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/cpuprofiler?enable=n", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/hot_restart_version", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/reset_counters", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  response = IntegrationUtil::makeSingleRequest(ADMIN_PORT, "GET", "/certs", "",
                                                Http::CodecClient::Type::HTTP1);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}
