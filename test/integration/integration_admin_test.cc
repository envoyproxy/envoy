#include "test/integration/integration_admin_test.h"

#include "envoy/http/header_map.h"

#include "common/common/fmt.h"
#include "common/json/json_loader.h"

#include "test/integration/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationAdminTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(IntegrationAdminTest, HealthCheck) {
  initialize();

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
}

TEST_P(IntegrationAdminTest, HealthCheckWithBufferFilter) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/healthcheck", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
}

TEST_P(IntegrationAdminTest, AdminLogging) {
  initialize();

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

namespace {

const char* ContentType(const BufferingStreamDecoderPtr& response) {
  const Http::HeaderEntry* entry = response->headers().ContentType();
  if (entry == nullptr) {
    return "(null)";
  }
  return entry->value().c_str();
}

} // namespace

TEST_P(IntegrationAdminTest, Admin) {
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/notfound", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("invalid path. admin commands are:"))
      << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/help", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("admin commands are:")) << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/html; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("<title>Envoy Admin</title>"))
      << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/server_info", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=blah",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  Json::ObjectSharedPtr statsjson = Json::Factory::loadFromString(response->body());
  EXPECT_TRUE(statsjson->hasObject("stats"));
  EXPECT_STREQ("application/json", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=prometheus", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_THAT(response->body(),
              testing::HasSubstr(
                  "envoy_http_downstream_rq_xx{envoy_response_code_class=\"4\",envoy_http_conn_"
                  "manager_prefix=\"admin\"} 2\n"));
  EXPECT_THAT(response->body(), testing::HasSubstr("# TYPE envoy_http_downstream_rq_xx counter\n"));
  EXPECT_THAT(response->body(),
              testing::HasSubstr(
                  "envoy_listener_admin_http_downstream_rq_xx{envoy_response_code_class=\"4\","
                  "envoy_http_conn_manager_prefix=\"admin\"} 2\n"));
  EXPECT_THAT(response->body(),
              testing::HasSubstr("# TYPE envoy_cluster_upstream_cx_active gauge\n"));
  EXPECT_THAT(
      response->body(),
      testing::HasSubstr("envoy_cluster_upstream_cx_active{envoy_cluster_name=\"cluster_0\"} 0\n"));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats/prometheus", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_THAT(response->body(),
              testing::HasSubstr(
                  "envoy_http_downstream_rq_xx{envoy_response_code_class=\"4\",envoy_http_conn_"
                  "manager_prefix=\"admin\"} 2\n"));
  EXPECT_THAT(response->body(), testing::HasSubstr("# TYPE envoy_http_downstream_rq_xx counter\n"));
  EXPECT_THAT(response->body(),
              testing::HasSubstr(
                  "envoy_listener_admin_http_downstream_rq_xx{envoy_response_code_class=\"4\","
                  "envoy_http_conn_manager_prefix=\"admin\"} 2\n"));
  EXPECT_THAT(response->body(),
              testing::HasSubstr("# TYPE envoy_cluster_upstream_cx_active gauge\n"));
  EXPECT_THAT(
      response->body(),
      testing::HasSubstr("envoy_cluster_upstream_cx_active{envoy_cluster_name=\"cluster_0\"} 0\n"));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/clusters", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_THAT(response->body(), testing::HasSubstr("added_via_api"));
  EXPECT_THAT(response->body(), testing::HasSubstr("version_info::static\n"));
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/cpuprofiler", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("400", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/hot_restart_version",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/reset_counters", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/certs", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/runtime", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/runtime?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("application/json", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/listeners", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  EXPECT_STREQ("application/json", ContentType(response));

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
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* admin = bootstrap.mutable_admin();
    admin->set_profile_path(TestEnvironment::temporaryPath("/envoy.prof"));
  });

  initialize();
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

class IntegrationAdminIpv4Ipv6Test : public HttpIntegrationTest, public testing::Test {
public:
  IntegrationAdminIpv4Ipv6Test()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, Network::Address::IpVersion::v4) {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          auto* socket_address =
              bootstrap.mutable_admin()->mutable_address()->mutable_socket_address();
          socket_address->set_ipv4_compat(true);
          socket_address->set_address("::");
        });
    HttpIntegrationTest::initialize();
  }
};

// Verify an IPv4 client can connect to the admin interface listening on :: when
// IPv4 compat mode is enabled.
TEST_F(IntegrationAdminIpv4Ipv6Test, Ipv4Ipv6Listen) {
  if (TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4) &&
      TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6)) {
    initialize();
    BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
        lookupPort("admin"), "GET", "/server_info", "", downstreamProtocol(), version_);
    EXPECT_TRUE(response->complete());
    EXPECT_STREQ("200", response->headers().Status()->value().c_str());
  }
}

} // namespace Envoy
