#include "test/integration/integration_admin_test.h"

#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/http/header_map.h"

#include "common/common/fmt.h"
#include "common/profiler/profiler.h"
#include "common/stats/stats_matcher_impl.h"

#include "test/integration/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(Protocols, IntegrationAdminTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(IntegrationAdminTest, HealthCheck) {
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "POST", "/healthcheck", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/healthcheck/fail",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/healthcheck/ok", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/healthcheck", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(IntegrationAdminTest, HealthCheckWithBufferFilter) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/healthcheck", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

TEST_P(IntegrationAdminTest, AdminLogging) {
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/logging", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  // Bad level
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/logging?level=blah",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());

  // Bad logger
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/logging?blah=info",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());

  // This is going to stomp over custom log levels that are set on the command line.
  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/logging?level=warning", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ("warning", logger.levelString());
  }

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/logging?assert=trace", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(spdlog::level::trace, Logger::Registry::getLog(Logger::Id::assert).level());

  spdlog::string_view_t level_name = spdlog::level::level_string_views[default_log_level_];
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST",
                                                fmt::format("/logging?level={}", level_name), "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    EXPECT_EQ(level_name, logger.levelString());
  }
}

namespace {

std::string ContentType(const BufferingStreamDecoderPtr& response) {
  const Http::HeaderEntry* entry = response->headers().ContentType();
  if (entry == nullptr) {
    return "(null)";
  }
  return std::string(entry->value().getStringView());
}

} // namespace

TEST_P(IntegrationAdminTest, Admin) {
  initialize();

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/notfound", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("invalid path. admin commands are:"))
      << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/help", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("admin commands are:")) << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/html; charset=UTF-8", ContentType(response));
  EXPECT_NE(std::string::npos, response->body().find("<title>Envoy Admin</title>"))
      << response->body();

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/server_info", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/ready", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?usedonly", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  // Testing a filter with no matches
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?filter=foo", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  // Testing a filter with matches
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?filter=server",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                "/stats?filter=server&usedonly", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response =
      IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=json&usedonly",
                                         "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));
  validateStatsJson(response->body(), 0);

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=blah",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("404", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", ContentType(response));
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  validateStatsJson(response->body(), 1);

  // Filtering stats by a regex with one match should return just that match.
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                "/stats?format=json&filter=^server\\.version$", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", ContentType(response));
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  validateStatsJson(response->body(), 0);
  EXPECT_THAT(response->body(),
              testing::Eq("{\"stats\":[{\"name\":\"server.version\",\"value\":0}]}"));

  // Filtering stats by a non-full-string regex should also return just that match.
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                "/stats?format=json&filter=server\\.version", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", ContentType(response));
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  validateStatsJson(response->body(), 0);
  EXPECT_THAT(response->body(),
              testing::Eq("{\"stats\":[{\"name\":\"server.version\",\"value\":0}]}"));

  // Filtering stats by a regex with no matches (".*not_intended_to_appear.*") should return a
  // valid, empty, stats array.
  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET",
                                                "/stats?format=json&filter=not_intended_to_appear",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("application/json", ContentType(response));
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  validateStatsJson(response->body(), 0);
  EXPECT_THAT(response->body(), testing::Eq("{\"stats\":[]}"));

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=prometheus", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
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
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_THAT(response->body(), testing::HasSubstr("added_via_api"));
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/clusters?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));
  EXPECT_NO_THROW(Json::Factory::loadFromString(response->body()));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/cpuprofiler", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/hot_restart_version",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "POST", "/reset_counters", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/certs", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/runtime", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/runtime_modify", "foo=bar&foo1=bar1", downstreamProtocol(),
      version_, "host", Http::Headers::get().ContentTypeValues.FormUrlEncoded);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/runtime?format=json",
                                                "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));

  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  auto entries = json->getObject("entries");
  auto foo_obj = entries->getObject("foo");
  EXPECT_EQ("bar", foo_obj->getString("final_value"));
  auto foo1_obj = entries->getObject("foo1");
  EXPECT_EQ("bar1", foo1_obj->getString("final_value"));

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/listeners", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("text/plain; charset=UTF-8", ContentType(response));
  auto listeners = test_server_->server().listenerManager().listeners();
  auto listener_it = listeners.cbegin();
  for (; listener_it != listeners.end(); ++listener_it) {
    EXPECT_THAT(response->body(), testing::HasSubstr(fmt::format(
                                      "{}::{}", listener_it->get().name(),
                                      listener_it->get().socket().localAddress()->asString())));
  }

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/listeners?format=json", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));

  json = Json::Factory::loadFromString(response->body());
  std::vector<Json::ObjectSharedPtr> listener_info = json->getObjectArray("listener_statuses");
  auto listener_info_it = listener_info.cbegin();
  listeners = test_server_->server().listenerManager().listeners();
  listener_it = listeners.cbegin();
  for (; listener_info_it != listener_info.end() && listener_it != listeners.end();
       ++listener_info_it, ++listener_it) {
    auto local_address = (*listener_info_it)->getObject("local_address");
    auto socket_address = local_address->getObject("socket_address");
    EXPECT_EQ(listener_it->get().socket().localAddress()->ip()->addressAsString(),
              socket_address->getString("address"));
    EXPECT_EQ(listener_it->get().socket().localAddress()->ip()->port(),
              socket_address->getInteger("port_value"));
  }

  response = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/config_dump", "",
                                                downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("application/json", ContentType(response));
  json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  const std::string expected_types[] = {
      "type.googleapis.com/envoy.admin.v2alpha.BootstrapConfigDump",
      "type.googleapis.com/envoy.admin.v2alpha.ClustersConfigDump",
      "type.googleapis.com/envoy.admin.v2alpha.ListenersConfigDump",
      "type.googleapis.com/envoy.admin.v2alpha.ScopedRoutesConfigDump",
      "type.googleapis.com/envoy.admin.v2alpha.RoutesConfigDump",
      "type.googleapis.com/envoy.admin.v2alpha.SecretsConfigDump"};

  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v2alpha::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(6, config_dump.configs_size());

  // .. and that we can unpack one of the entries.
  envoy::admin::v2alpha::RoutesConfigDump route_config_dump;
  config_dump.configs(4).UnpackTo(&route_config_dump);
  EXPECT_EQ("route_config_0", route_config_dump.static_route_configs(0).route_config().name());

  envoy::admin::v2alpha::SecretsConfigDump secret_config_dump;
  config_dump.configs(5).UnpackTo(&secret_config_dump);
  EXPECT_EQ("secret_static_0", secret_config_dump.static_secrets(0).name());
}

TEST_P(IntegrationAdminTest, AdminOnDestroyCallbacks) {
  initialize();
  bool test = true;

  // add an handler which adds a callback to the list of callback called when connection is dropped.
  auto callback = [&test](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                          Server::AdminStream& admin_stream) -> Http::Code {
    auto on_destroy_callback = [&test]() { test = false; };

    // Add the on_destroy_callback to the admin_filter list of callbacks.
    admin_stream.addOnDestroyCallback(std::move(on_destroy_callback));
    return Http::Code::OK;
  };

  EXPECT_TRUE(
      test_server_->server().admin().addHandler("/foo/bar", "hello", callback, true, false));

  // As part of the request, on destroy() should be called and the on_destroy_callback invoked.
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/foo/bar", "", downstreamProtocol(), version_);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  // Check that the added callback was invoked.
  EXPECT_EQ(test, false);

  // Small test to cover new statsFlushInterval() on Instance.h.
  EXPECT_EQ(test_server_->server().statsFlushInterval(), std::chrono::milliseconds(5000));
}

TEST_P(IntegrationAdminTest, AdminCpuProfilerStart) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
    auto* admin = bootstrap.mutable_admin();
    admin->set_profile_path(TestEnvironment::temporaryPath("/envoy.prof"));
  });

  initialize();
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/cpuprofiler?enable=y", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
#else
  EXPECT_EQ("500", response->headers().Status()->value().getStringView());
#endif

  response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/cpuprofiler?enable=n", "", downstreamProtocol(), version_);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
}

class IntegrationAdminIpv4Ipv6Test : public testing::Test, public HttpIntegrationTest {
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
    EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  }
}

// Testing the behavior of StatsMatcher, which allows/denies the  instantiation of stats based on
// restrictions on their names.
class StatsMatcherIntegrationTest
    : public testing::Test,
      public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest,
      public testing::WithParamInterface<Network::Address::IpVersion> {
public:
  StatsMatcherIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier(
        [this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
          *bootstrap.mutable_stats_config()->mutable_stats_matcher() = stats_matcher_;
        });
    HttpIntegrationTest::initialize();
  }
  void makeRequest() {
    response_ = IntegrationUtil::makeSingleRequest(lookupPort("admin"), "GET", "/stats", "",
                                                   downstreamProtocol(), version_);
    ASSERT_TRUE(response_->complete());
    EXPECT_EQ("200", response_->headers().Status()->value().getStringView());
  }

  BufferingStreamDecoderPtr response_;
  envoy::config::metrics::v2::StatsMatcher stats_matcher_;
};
INSTANTIATE_TEST_SUITE_P(IpVersions, StatsMatcherIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that StatsMatcher prevents the printing of uninstantiated stats.
TEST_P(StatsMatcherIntegrationTest, ExcludePrefixServerDot) {
  stats_matcher_.mutable_exclusion_list()->add_patterns()->set_prefix("server.");
  initialize();
  makeRequest();
  EXPECT_THAT(response_->body(), testing::Not(testing::HasSubstr("server.")));
}

TEST_P(StatsMatcherIntegrationTest, ExcludeRequests) {
  stats_matcher_.mutable_exclusion_list()->add_patterns()->set_regex(".*requests.*");
  initialize();
  makeRequest();
  EXPECT_THAT(response_->body(), testing::Not(testing::HasSubstr("requests")));
}

TEST_P(StatsMatcherIntegrationTest, ExcludeExact) {
  stats_matcher_.mutable_exclusion_list()->add_patterns()->set_exact("server.concurrency");
  initialize();
  makeRequest();
  EXPECT_THAT(response_->body(), testing::Not(testing::HasSubstr("server.concurrency")));
}

TEST_P(StatsMatcherIntegrationTest, ExcludeMultipleExact) {
  stats_matcher_.mutable_exclusion_list()->add_patterns()->set_exact("server.concurrency");
  stats_matcher_.mutable_exclusion_list()->add_patterns()->set_regex(".*live");
  initialize();
  makeRequest();
  EXPECT_THAT(response_->body(), testing::Not(testing::HasSubstr("server.concurrency")));
  EXPECT_THAT(response_->body(), testing::Not(testing::HasSubstr("server.live")));
}

// TODO(ambuc): Find a cleaner way to test this. This test has an unfortunate compromise:
// `listener_manager.listener_create_success` must be instantiated, because BaseIntegrationTest
// blocks on its creation (see waitForCounterGe and the suite of waitFor* functions).
// If this invariant is changed, this test must be rewritten.
TEST_P(StatsMatcherIntegrationTest, IncludeExact) {
  // Stats matching does not play well with LDS, at least in test. See #7215.
  use_lds_ = false;
  stats_matcher_.mutable_inclusion_list()->add_patterns()->set_exact(
      "listener_manager.listener_create_success");
  initialize();
  makeRequest();
  EXPECT_EQ(response_->body(), "listener_manager.listener_create_success: 1\n");
}

} // namespace Envoy
