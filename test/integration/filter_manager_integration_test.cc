#include <regex>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/common/factory_base.h"

#include "test/integration/filter_manager_integration_test.pb.h"
#include "test/integration/filter_manager_integration_test.pb.validate.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

/**
 * Basic traffic throttler that emits a next chunk of the original request/response data
 * on timer tick.
 */
class Throttler {
public:
  Throttler(Event::Dispatcher& dispatcher, std::chrono::milliseconds tick_interval,
            uint64_t max_chunk_length, std::function<void(Buffer::Instance&, bool)> next_chunk_cb)
      : timer_(dispatcher.createTimer([this] { onTimerTick(); })), tick_interval_(tick_interval),
        max_chunk_length_(max_chunk_length), next_chunk_cb_(next_chunk_cb) {}

  /**
   * Throttle given given request/response data.
   */
  void throttle(Buffer::Instance& data, bool end_stream);
  /**
   * Cancel any scheduled activities (on connection close).
   */
  void reset();

private:
  void onTimerTick();

  Buffer::OwnedImpl buffer_{};
  bool end_stream_{};

  const Event::TimerPtr timer_;
  const std::chrono::milliseconds tick_interval_;
  const uint64_t max_chunk_length_;
  const std::function<void(Buffer::Instance&, bool)> next_chunk_cb_;
};

void Throttler::throttle(Buffer::Instance& data, bool end_stream) {
  buffer_.move(data);
  end_stream_ |= end_stream;
  if (!timer_->enabled()) {
    timer_->enableTimer(tick_interval_);
  }
}

void Throttler::reset() { timer_->disableTimer(); }

void Throttler::onTimerTick() {
  Buffer::OwnedImpl next_chunk{};
  if (0 < buffer_.length()) {
    auto chunk_length = max_chunk_length_ < buffer_.length() ? max_chunk_length_ : buffer_.length();
    next_chunk.move(buffer_, chunk_length);
  }
  bool end_stream = end_stream_ && 0 == buffer_.length();
  if (0 < buffer_.length()) {
    timer_->enableTimer(tick_interval_);
  }
  next_chunk_cb_(next_chunk, end_stream);
}

/**
 * Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
 * and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of a timer
 * callback.
 *
 * Emits a next chunk of the original request/response data on timer tick.
 */
class ThrottlerFilter : public Network::Filter, public Network::ConnectionCallbacks {
public:
  ThrottlerFilter(std::chrono::milliseconds tick_interval, uint64_t max_chunk_length)
      : tick_interval_(tick_interval), max_chunk_length_(max_chunk_length) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);

    read_throttler_ = std::make_unique<Throttler>(
        read_callbacks_->connection().dispatcher(), tick_interval_, max_chunk_length_,
        [this](Buffer::Instance& data, bool end_stream) {
          read_callbacks_->injectReadDataToFilterChain(data, end_stream);
        });
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;

    write_throttler_ = std::make_unique<Throttler>(
        write_callbacks_->connection().dispatcher(), tick_interval_, max_chunk_length_,
        [this](Buffer::Instance& data, bool end_stream) {
          write_callbacks_->injectWriteDataToFilterChain(data, end_stream);
        });
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};

  std::unique_ptr<Throttler> read_throttler_;
  std::unique_ptr<Throttler> write_throttler_;

  const std::chrono::milliseconds tick_interval_;
  const uint64_t max_chunk_length_;
};

// Network::ReadFilter
Network::FilterStatus ThrottlerFilter::onNewConnection() { return Network::FilterStatus::Continue; }

Network::FilterStatus ThrottlerFilter::onData(Buffer::Instance& data, bool end_stream) {
  read_throttler_->throttle(data, end_stream);
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::WriteFilter
Network::FilterStatus ThrottlerFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  write_throttler_->throttle(data, end_stream);
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::ConnectionCallbacks
void ThrottlerFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    read_throttler_->reset();
    write_throttler_->reset();
  }
}

/**
 * Config factory for ThrottlerFilter.
 */
class ThrottlerFilterConfigFactory : public Extensions::NetworkFilters::Common::FactoryBase<
                                         test::integration::filter_manager::Throttler> {
public:
  explicit ThrottlerFilterConfigFactory(const std::string& name) : FactoryBase(name) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filter_manager::Throttler& proto_config,
      Server::Configuration::FactoryContext&) override {
    return [proto_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<ThrottlerFilter>(
          std::chrono::milliseconds(proto_config.tick_interval_ms()),
          proto_config.max_chunk_length()));
    };
  }
};

/**
 * Auxiliary network filter that makes use of ReadFilterCallbacks::injectReadDataToFilterChain()
 * and WriteFilterCallbacks::injectWriteDataToFilterChain() methods in the context of
 * ReadFilter::onData() and WriteFilter::onWrite().
 *
 * Calls ReadFilterCallbacks::injectReadDataToFilterChain() /
 * WriteFilterCallbacks::injectWriteDataToFilterChain() to pass data to the next filter
 * byte-by-byte.
 */
class DispenserFilter : public Network::Filter {
public:
  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  // Pass data to the next filter byte-by-byte.
  void dispense(Buffer::Instance& data, bool end_stream,
                std::function<void(Buffer::Instance&, bool)> next_chunk_cb);

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
};

// Network::ReadFilter
Network::FilterStatus DispenserFilter::onNewConnection() { return Network::FilterStatus::Continue; }

Network::FilterStatus DispenserFilter::onData(Buffer::Instance& data, bool end_stream) {
  dispense(data, end_stream, [this](Buffer::Instance& data, bool end_stream) {
    read_callbacks_->injectReadDataToFilterChain(data, end_stream);
  });
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Network::WriteFilter
Network::FilterStatus DispenserFilter::onWrite(Buffer::Instance& data, bool end_stream) {
  dispense(data, end_stream, [this](Buffer::Instance& data, bool end_stream) {
    write_callbacks_->injectWriteDataToFilterChain(data, end_stream);
  });
  ASSERT(data.length() == 0);
  return Network::FilterStatus::StopIteration;
}

// Pass data to the next filter byte-by-byte.
void DispenserFilter::dispense(Buffer::Instance& data, bool end_stream,
                               std::function<void(Buffer::Instance&, bool)> next_chunk_cb) {
  Buffer::OwnedImpl next_chunk{};
  do {
    if (0 < data.length()) {
      next_chunk.move(data, 1);
    }
    next_chunk_cb(next_chunk, end_stream && 0 == data.length());
    next_chunk.drain(next_chunk.length());
  } while (0 < data.length());
}

/**
 * Config factory for DispenserFilter.
 */
class DispenserFilterConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  explicit DispenserFilterConfigFactory(const std::string& name) : name_(name) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<DispenserFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return name_; }

private:
  const std::string name_;
};

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
  static std::string echoConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
      - name: envoy.filters.network.echo
      )EOF");
  }

  InjectDataWithEchoFilterIntegrationTest()
      : InjectDataToFilterChainIntegrationTest(echoConfig()) {}
};

INSTANTIATE_TEST_SUITE_P(
    Params, InjectDataWithEchoFilterIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(auxiliary_filters())),
    InjectDataToFilterChainIntegrationTest::testParamsToString);

TEST_P(InjectDataWithEchoFilterIntegrationTest, UsageOfInjectDataMethodsShouldBeUnnoticeable) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  tcp_client->waitForData("hello");

  tcp_client->close();
}

TEST_P(InjectDataWithEchoFilterIntegrationTest, FilterChainMismatch) {
  useListenerAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE_DETAILS%");
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_filter_chains(0)
        ->mutable_filter_chain_match()
        ->set_transport_protocol("tls");
  });
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello", false, false));

  std::string access_log =
      absl::StrCat("NR ", StreamInfo::ResponseCodeDetails::get().FilterChainNotFound);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::HasSubstr(access_log));
  tcp_client->waitForDisconnect();
}

/**
 * Integration test with an auxiliary filter in front of "envoy.filters.network.tcp_proxy".
 */
class InjectDataWithTcpProxyFilterIntegrationTest : public InjectDataToFilterChainIntegrationTest {
public:
  InjectDataWithTcpProxyFilterIntegrationTest()
      : InjectDataToFilterChainIntegrationTest(ConfigHelper::tcpProxyConfig()) {}
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

  ASSERT_TRUE(tcp_client->write("hello"));

  std::string observed_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(5, &observed_data));
  EXPECT_EQ("hello", observed_data);

  ASSERT_TRUE(fake_upstream_connection->write("hi"));
  tcp_client->waitForData("hi");

  ASSERT_TRUE(tcp_client->write(" world!", true));
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
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("greetings", response->body());
}

} // namespace
} // namespace Envoy
