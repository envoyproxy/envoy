#include "test/integration/filter_manager_integration_test.h"

#include <regex>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
// and WriteFilterCallbacks::injectWriteDataToFilterChain() methods outside of the context of
// ReadFilter::onData() and WriteFilter::onWrite(), i.e. on timer event
const char inject_data_outside_callback_filter[] = "inject-data-outside-filter-callback";

// Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
// and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of
// ReadFilter::onData() and WriteFilter::onWrite()
const char inject_data_inside_callback_filter[] = "inject-data-inside-filter-callback";

// Do not use ReadFilterCallbacks::injectReadDataToFilterChain() and
// WriteFilterCallbacks::injectWriteDataToFilterChain() methods at all
const char no_inject_data[] = "no-inject-data";

// List of auxiliary filters to test against
const std::vector<std::string> auxiliary_filters() {
  return {inject_data_outside_callback_filter, inject_data_inside_callback_filter, no_inject_data};
}

// Used to pretty print test parameters
const std::regex invalid_param_name_regex() { return std::regex{"[^a-zA-Z0-9_]"}; }

/**
 * Integration test with one of auxiliary filters (listed above)
 * added to the head of the filter chain.
 *
 * Shared by tests for "envoy.filters.network.echo", "envoy.filters.network.tcp_proxy" and
 * "envoy.filters.network.http_connection_manager".
 */
class TestWithAuxiliaryFilter {
public:
  explicit TestWithAuxiliaryFilter(const std::string& auxiliary_filter_name)
      : auxiliary_filter_name_(auxiliary_filter_name) {}

  virtual ~TestWithAuxiliaryFilter() = default;

protected:
  /**
   * Returns configuration for a given auxiliary filter.
   *
   * Assuming that representative configurations differ in the context of
   * "envoy.filters.network.echo", "envoy.filters.network.tcp_proxy" and
   * "envoy.filters.network.http_connection_manager".
   */
  virtual std::string filterConfig(const std::string& auxiliary_filter_name) PURE;

  /**
   * Adds an auxiliary filter to the head of the filter chain.
   * @param config_helper helper object.
   */
  void addAuxiliaryFilter(ConfigHelper& config_helper) {
    if (auxiliary_filter_name_ == no_inject_data) {
      // we want to run the same test on unmodified filter chain and observe identical behaviour
      return;
    }
    addNetworkFilter(config_helper, fmt::format(R"EOF(
      name: {}
    )EOF",
                                                auxiliary_filter_name_) +
                                        filterConfig(auxiliary_filter_name_));
    // double-check the filter was actually added
    config_helper.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ASSERT_EQ(auxiliary_filter_name_,
                bootstrap.static_resources().listeners(0).filter_chains(0).filters(0).name());
    });
  }

  /**
   * Add a network filter prior to existing filters.
   * @param config_helper helper object.
   * @param filter_yaml configuration snippet.
   */
  void addNetworkFilter(ConfigHelper& config_helper, const std::string& filter_yaml) {
    config_helper.addConfigModifier(
        [filter_yaml](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          ASSERT_GT(bootstrap.mutable_static_resources()->listeners_size(), 0);
          auto l = bootstrap.mutable_static_resources()->mutable_listeners(0);
          ASSERT_GT(l->filter_chains_size(), 0);

          auto* filter_chain = l->mutable_filter_chains(0);
          auto* filter_list_back = filter_chain->add_filters();
          TestUtility::loadFromYaml(filter_yaml, *filter_list_back);

          // Now move it to the front.
          for (int i = filter_chain->filters_size() - 1; i > 0; --i) {
            filter_chain->mutable_filters()->SwapElements(i, i - 1);
          }
        });
  }

private:
  // one of auxiliary filters (listed at the top)
  const std::string auxiliary_filter_name_;

  // Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
  // and WriteFilterCallbacks::injectWriteDataToFilterChain() methods outside of the context of
  // ReadFilter::onData() and WriteFilter::onWrite(), i.e. on timer event
  ThrottlerFilterConfigFactory outside_callback_config_factory_{
      inject_data_outside_callback_filter};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_outside_callback_config_factory_{outside_callback_config_factory_};

  // Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
  // and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of
  // ReadFilter::onData() and WriteFilter::onWrite()
  DispenserFilterConfigFactory inside_callback_config_factory_{inject_data_inside_callback_filter};
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_inside_callback_config_factory_{inside_callback_config_factory_};
};

/**
 * Base class for "envoy.filters.network.echo" and "envoy.filters.network.tcp_proxy" tests.
 *
 * Inherits from BaseIntegrationTest; parameterized with IP version and auxiliary filter.
 */
class InjectDataToFilterChainIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, std::string>>,
      public BaseIntegrationTest,
      public TestWithAuxiliaryFilter {
public:
  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_no_inject_data
  static std::string testParamsToString(
      const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, std::string>>& params) {
    return fmt::format(
        "{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        std::regex_replace(std::get<1>(params.param), invalid_param_name_regex(), "_"));
  }

  explicit InjectDataToFilterChainIntegrationTest(const std::string& config)
      : BaseIntegrationTest(std::get<0>(GetParam()), config),
        TestWithAuxiliaryFilter(std::get<1>(GetParam())) {}

  void SetUp() override { addAuxiliaryFilter(config_helper_); }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

protected:
  // Returns configuration for a given auxiliary filter
  std::string filterConfig(const std::string& auxiliary_filter_name) override {
    return auxiliary_filter_name == inject_data_outside_callback_filter ? R"EOF(
      typed_config:
        "@type": type.googleapis.com/test.integration.filter_manager.Throttler
        tick_interval_ms: 1
        max_chunk_length: 5
    )EOF"
                                                                        : "";
  }
};

/**
 * Integration test with an auxiliary filter in front of "envoy.filters.network.echo".
 */
class InjectDataWithEchoFilterIntegrationTest : public InjectDataToFilterChainIntegrationTest {
public:
  static std::string echo_config() {
    return ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
      - name: envoy.filters.network.echo
      )EOF";
  }

  InjectDataWithEchoFilterIntegrationTest()
      : InjectDataToFilterChainIntegrationTest(echo_config()) {}
};

INSTANTIATE_TEST_SUITE_P(
    Params, InjectDataWithEchoFilterIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(auxiliary_filters())),
    InjectDataToFilterChainIntegrationTest::testParamsToString);

TEST_P(InjectDataWithEchoFilterIntegrationTest, UsageOfInjectDataMethodsShouldBeUnnoticeable) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write("hello");
  tcp_client->waitForData("hello");

  tcp_client->close();
}

/**
 * Integration test with an auxiliary filter in front of "envoy.filters.network.tcp_proxy".
 */
class InjectDataWithTcpProxyFilterIntegrationTest : public InjectDataToFilterChainIntegrationTest {
public:
  InjectDataWithTcpProxyFilterIntegrationTest()
      : InjectDataToFilterChainIntegrationTest(ConfigHelper::TCP_PROXY_CONFIG) {}
};

INSTANTIATE_TEST_SUITE_P(
    Params, InjectDataWithTcpProxyFilterIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(auxiliary_filters())),
    InjectDataToFilterChainIntegrationTest::testParamsToString);

TEST_P(InjectDataWithTcpProxyFilterIntegrationTest, UsageOfInjectDataMethodsShouldBeUnnoticeable) {
  enable_half_close_ = true;
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  tcp_client->write("hello");

  std::string observed_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &observed_data));
  EXPECT_EQ("hello", observed_data);

  ASSERT_TRUE(fake_upstream_connection->write("hi"));
  tcp_client->waitForData("hi");

  tcp_client->write(" world!", true);
  observed_data.clear();
  ASSERT_TRUE(fake_upstream_connection->waitForData(12, &observed_data));
  EXPECT_EQ("hello world!", observed_data);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->write("there!", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect(true));

  tcp_client->waitForData("there!");
  tcp_client->waitForDisconnect();
}

/**
 * Integration test with an auxiliary filter in front of
 * "envoy.filters.network.http_connection_manager".
 *
 * Inherits from HttpIntegrationTest;
 * parameterized with IP version, downstream HTTP version and auxiliary filter.
 */
class InjectDataWithHttpConnectionManagerIntegrationTest
    : public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, Http::CodecClient::Type, std::string>>,
      public HttpIntegrationTest,
      public TestWithAuxiliaryFilter {
public:
  // Allows pretty printed test names of the form
  // FooTestCase.BarInstance/IPv4_Http_no_inject_data
  static std::string testParamsToString(
      const testing::TestParamInfo<
          std::tuple<Network::Address::IpVersion, Http::CodecClient::Type, std::string>>& params) {
    return fmt::format(
        "{}_{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        (std::get<1>(params.param) == Http::CodecClient::Type::HTTP2 ? "Http2" : "Http"),
        std::regex_replace(std::get<2>(params.param), invalid_param_name_regex(), "_"));
  }

  InjectDataWithHttpConnectionManagerIntegrationTest()
      : HttpIntegrationTest(std::get<1>(GetParam()), std::get<0>(GetParam())),
        TestWithAuxiliaryFilter(std::get<2>(GetParam())) {}

  void SetUp() override { addAuxiliaryFilter(config_helper_); }

  void TearDown() override {
    // already cleaned up in ~HttpIntegrationTest()
  }

protected:
  // Returns configuration for a given auxiliary filter
  std::string filterConfig(const std::string& auxiliary_filter_name) override {
    return auxiliary_filter_name == inject_data_outside_callback_filter ? R"EOF(
      typed_config:
        "@type": type.googleapis.com/test.integration.filter_manager.Throttler
        tick_interval_ms: 1
        max_chunk_length: 10
    )EOF"
                                                                        : "";
  }
};

INSTANTIATE_TEST_SUITE_P(
    Params, InjectDataWithHttpConnectionManagerIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(Http::CodecClient::Type::HTTP1,
                                     Http::CodecClient::Type::HTTP2),
                     testing::ValuesIn(auxiliary_filters())),
    InjectDataWithHttpConnectionManagerIntegrationTest::testParamsToString);

TEST_P(InjectDataWithHttpConnectionManagerIntegrationTest,
       UsageOfInjectDataMethodsShouldBeUnnoticeable) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/api"}, {":authority", "host"}, {":scheme", "http"}};
  auto response = codec_client_->makeRequestWithBody(headers, "hello!");

  waitForNextUpstreamRequest();
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("hello!", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  Buffer::OwnedImpl response_data{"greetings"};
  upstream_request_->encodeData(response_data, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ("greetings", response->body());
}

} // namespace
} // namespace Envoy
