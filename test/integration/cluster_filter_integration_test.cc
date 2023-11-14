#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Helper class for passing function call order information from a test filter to the test class.
// May only be called from one filter during a test.
class TestParent {
public:
  virtual ~TestParent() = default;

  // Called from a worker thread
  virtual void onNewConnectionCalled(bool on_write_called) PURE;
};

class PoliteFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  PoliteFilter(TestParent& parent, const ProtobufWkt::StringValue& value)
      : parent_(parent), greeting_(value.value()) {}

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onData {} bytes {} end_stream", read_callbacks_->connection(),
                   data.length(), end_stream);
    if (!read_greeted_) {
      if (greeting_.length() > 0) {
        Buffer::OwnedImpl greeter(greeting_);
        read_callbacks_->injectReadDataToFilterChain(greeter, false);
      }
      read_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onWrite {} bytes {} end_stream", write_callbacks_->connection(),
                   data.length(), end_stream);
    if (!write_greeted_) {
      if (greeting_.length() > 0) {
        Buffer::OwnedImpl greeter("please ");
        write_callbacks_->injectWriteDataToFilterChain(greeter, false);
      }
      write_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    ENVOY_CONN_LOG(debug, "polite: new connection", read_callbacks_->connection());
    parent_.onNewConnectionCalled(write_greeted_);
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  TestParent& parent_;
  const std::string greeting_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  bool read_greeted_{false};
  bool write_greeted_{false};
};

class PoliteFilterConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  PoliteFilterConfigFactory(TestParent& test_parent) : test_parent_(test_parent) {}

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::UpstreamFactoryContext&) override {
    auto config = dynamic_cast<const ProtobufWkt::StringValue&>(proto_config);
    return [this, config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<PoliteFilter>(test_parent_, config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "envoy.upstream.polite"; }

private:
  TestParent& test_parent_;
};

std::string ipInitializeUpstreamFiltersTestParamsToString(
    const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(
          testing::TestParamInfo<Network::Address::IpVersion>(std::get<0>(params.param), 0)),
      std::get<1>(params.param) ? "do_initialize_upstream_filters"
                                : "dont_initialize_upstream_filters");
}

class ClusterFilterIntegrationTestBase
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public TestParent {
public:
  ClusterFilterIntegrationTestBase() : factory_(*this), registration_(factory_) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.initialize_upstream_filters",
                                  std::get<1>(GetParam()));
  }

  // Get the test parameter whether upstream network filters are initialized right after the
  // upstream connection has been established
  bool upstreamFiltersInitializedWhenConnected() const { return std::get<1>(GetParam()); }

  void initialize() { on_new_connection_called_after_on_write_.store(absl::optional<bool>{}); }

  // TestParent
  void onNewConnectionCalled(bool on_write_called) override {
    on_new_connection_called_after_on_write_.store(on_write_called);
  }

  bool wasOnNewConnectionCalled() {
    return on_new_connection_called_after_on_write_.load().has_value();
  }

  bool wasOnNewConnectionCalledFirst() {
    auto const& atomic_optional_bool = on_new_connection_called_after_on_write_.load();
    // return 'false' instead of throwing an exception if the value has not been set yet.
    return atomic_optional_bool.has_value() && !atomic_optional_bool.value();
  }

  PoliteFilterConfigFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>
      registration_;

private:
  // Atomic so that this may be safely accessed from multiple threads
  std::atomic<absl::optional<bool>> on_new_connection_called_after_on_write_{};
};

class ClusterFilterTcpIntegrationTest : public ClusterFilterIntegrationTestBase,
                                        public BaseIntegrationTest {
public:
  ClusterFilterTcpIntegrationTest()
      : BaseIntegrationTest(std::get<0>(GetParam()), ConfigHelper::tcpProxyConfig()) {}

  void initialize() override {
    enableHalfClose(true);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster_0->add_filters();
      filter->set_name("envoy.upstream.polite");
      ProtobufWkt::StringValue config;
      config.set_value("surely ");
      filter->mutable_typed_config()->PackFrom(config);
    });
    ClusterFilterIntegrationTestBase::initialize();
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsInitializeUpstreamFilters, ClusterFilterTcpIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipInitializeUpstreamFiltersTestParamsToString);

TEST_P(ClusterFilterTcpIntegrationTest, TestClusterFilter) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("test"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(11, &observed_data));
  EXPECT_EQ("please test", observed_data);

  // Upstream read filters are expected to be initialized at this point after connection has been
  // established but nothing has been read from the connection yet, but only if runtime feature
  // flag 'initialize_upstream_filters' is true.
  if (upstreamFiltersInitializedWhenConnected()) {
    // Note that we need to have written on the connection and waited for the written data to be
    // received so that we know the upstream connection has had chance to schedule the
    // onNewConnection() callbacks. This test will be flaky if we expect onNewConnection() having
    // been called before the waitForData() above.
    ASSERT_TRUE(wasOnNewConnectionCalled());
    ASSERT_TRUE(wasOnNewConnectionCalledFirst());
  } else {
    ASSERT_FALSE(wasOnNewConnectionCalled());
  }

  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" everything"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(22, &observed_data));
  EXPECT_EQ("please test everything", observed_data);

  ASSERT_TRUE(fake_upstream_connection->write("yes"));
  tcp_client->waitForData("surely yes");

  // Finally after reading from the upstream connection onNewConnection() has been called in all
  // cases, but onWrite was called before it if runtime feature flag 'initialize_upstream_filters'
  // is false.
  ASSERT_TRUE(wasOnNewConnectionCalled());
  if (!upstreamFiltersInitializedWhenConnected()) {
    ASSERT_FALSE(wasOnNewConnectionCalledFirst());
  }

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();
}

class ClusterFilterHttpIntegrationTest : public ClusterFilterIntegrationTestBase,
                                         public HttpIntegrationTest {
public:
  ClusterFilterHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster_0->add_filters();
      filter->set_name("envoy.upstream.polite");
      ProtobufWkt::StringValue config;
      config.set_value("");
      filter->mutable_typed_config()->PackFrom(config);
    });
    ClusterFilterIntegrationTestBase::initialize();
    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsInitializeUpstreamFilters, ClusterFilterHttpIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipInitializeUpstreamFiltersTestParamsToString);

TEST_P(ClusterFilterHttpIntegrationTest, TestClusterFilter) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/api"}, {":authority", "host"}, {":scheme", "http"}};
  auto response = codec_client_->makeRequestWithBody(headers, "hello!");

  waitForNextUpstreamRequest();

  // Upstream read filters are expected to be initialized at this point after only the request has
  // been sent, but only if runtime feature flag 'initialize_upstream_filters' is true.
  if (upstreamFiltersInitializedWhenConnected()) {
    ASSERT_TRUE(wasOnNewConnectionCalled());
    ASSERT_TRUE(wasOnNewConnectionCalledFirst());
  } else {
    ASSERT_FALSE(wasOnNewConnectionCalled());
  }

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("hello!", upstream_request_->body().toString());

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  Buffer::OwnedImpl response_data{"greetings"};
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  // Finally after receiving the response onNewConnection() has been called in all cases, but
  // onWrite was called before it if runtime feature flag 'initialize_upstream_filters' is false.
  ASSERT_TRUE(wasOnNewConnectionCalled());
  if (upstreamFiltersInitializedWhenConnected()) {
    ASSERT_TRUE(wasOnNewConnectionCalledFirst());
  } else {
    ASSERT_FALSE(wasOnNewConnectionCalledFirst());
  }

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("greetings", response->body());
}

} // namespace
} // namespace Envoy
