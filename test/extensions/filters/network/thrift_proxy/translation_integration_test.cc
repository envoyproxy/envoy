#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"

#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/network/thrift_proxy/integration.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::Combine;
using ::testing::TestParamInfo;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftTranslationIntegrationTest
    : public testing::TestWithParam<
          std::tuple<TransportType, ProtocolType, TransportType, ProtocolType, bool>>,
      public BaseThriftIntegrationTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    thrift_config_ = absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: thrift
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.ThriftProxy
            stat_prefix: thrift_stats
            route_config:
              name: "routes"
              routes:
                - match:
                    method_name: "add"
                  route:
                    cluster: "cluster_0"
      )EOF");
  }

  void initialize() override {
    std::tie(downstream_transport_, downstream_protocol_, upstream_transport_, upstream_protocol_,
             passthrough_) = GetParam();

    auto upstream_transport_proto = transportTypeToProto(upstream_transport_);
    auto upstream_protocol_proto = protocolTypeToProto(upstream_protocol_);

    envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions proto_opts;
    proto_opts.set_transport(upstream_transport_proto);
    proto_opts.set_protocol(upstream_protocol_proto);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* opts = bootstrap.mutable_static_resources()
                       ->mutable_clusters(0)
                       ->mutable_typed_extension_protocol_options();
      (*opts)[NetworkFilterNames::get().ThriftProxy].PackFrom(proto_opts);
    });

    if (passthrough_) {
      config_helper_.addFilterConfigModifier<
          envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy>(
          "thrift", [](Protobuf::Message& filter) {
            auto& conn_manager =
                dynamic_cast<envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy&>(
                    filter);
            conn_manager.set_payload_passthrough(true);
          });
    }

    // Invent some varying, but deterministic, values to add. We use the add method instead of
    // execute because the default execute params contains a set and the ordering can vary across
    // generated payloads.
    std::vector<std::string> args({
        fmt::format("{}", (static_cast<int>(downstream_transport_) << 8) +
                              static_cast<int>(downstream_protocol_)),
        fmt::format("{}", (static_cast<int>(upstream_transport_) << 8) +
                              static_cast<int>(upstream_protocol_)),
    });

    PayloadOptions downstream_opts(downstream_transport_, downstream_protocol_, DriverMode::Success,
                                   {}, "add", args);
    preparePayloads(downstream_opts, downstream_request_bytes_, downstream_response_bytes_);

    PayloadOptions upstream_opts(upstream_transport_, upstream_protocol_, DriverMode::Success, {},
                                 "add", args);
    preparePayloads(upstream_opts, upstream_request_bytes_, upstream_response_bytes_);

    BaseThriftIntegrationTest::initialize();
  }

  TransportType downstream_transport_;
  ProtocolType downstream_protocol_;
  TransportType upstream_transport_;
  ProtocolType upstream_protocol_;
  bool passthrough_;
  Buffer::OwnedImpl downstream_request_bytes_;
  Buffer::OwnedImpl downstream_response_bytes_;
  Buffer::OwnedImpl upstream_request_bytes_;
  Buffer::OwnedImpl upstream_response_bytes_;
};

static std::string paramToString(
    const TestParamInfo<std::tuple<TransportType, ProtocolType, TransportType, ProtocolType, bool>>&
        params) {
  TransportType downstream_transport, upstream_transport;
  ProtocolType downstream_protocol, upstream_protocol;
  bool passthrough;
  std::tie(downstream_transport, downstream_protocol, upstream_transport, upstream_protocol,
           passthrough) = params.param;

  auto result =
      fmt::format("From{}{}To{}{}", transportNameForTest(downstream_transport),
                  protocolNameForTest(downstream_protocol),
                  transportNameForTest(upstream_transport), protocolNameForTest(upstream_protocol));
  if (passthrough) {
    result = fmt::format("{}Passthrough", result);
  }
  return result;
}

INSTANTIATE_TEST_SUITE_P(
    TransportsAndProtocols, ThriftTranslationIntegrationTest,
    Combine(Values(TransportType::Framed, TransportType::Unframed, TransportType::Header),
            Values(ProtocolType::Binary, ProtocolType::Compact),
            Values(TransportType::Framed, TransportType::Unframed, TransportType::Header),
            Values(ProtocolType::Binary, ProtocolType::Compact), Values(false, true)),
    paramToString);

// Tests that the proxy will translate between different downstream and upstream transports and
// protocols.
TEST_P(ThriftTranslationIntegrationTest, Translates) {
  DISABLE_UNDER_WINDOWS; // https://github.com/envoyproxy/envoy/issues/21017
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write(downstream_request_bytes_.toString()));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(upstream_request_bytes_.length(), &data));
  Buffer::OwnedImpl upstream_request(data);
  EXPECT_EQ(upstream_request_bytes_.toString(), upstream_request.toString());

  ASSERT_TRUE(fake_upstream_connection->write(upstream_response_bytes_.toString()));

  tcp_client->waitForData(downstream_response_bytes_.toString());
  tcp_client->close();

  EXPECT_TRUE(
      TestUtility::buffersEqual(Buffer::OwnedImpl(tcp_client->data()), downstream_response_bytes_));

  Stats::CounterSharedPtr counter = test_server_->counter("thrift.thrift_stats.request_call");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("cluster.cluster_0.thrift.upstream_rq_call");
  EXPECT_EQ(1U, counter->value());
  if (passthrough_ &&
      (downstream_transport_ == TransportType::Framed ||
       downstream_transport_ == TransportType::Header) &&
      (upstream_transport_ == TransportType::Framed ||
       upstream_transport_ == TransportType::Header) &&
      downstream_protocol_ == upstream_protocol_ && downstream_protocol_ != ProtocolType::Twitter) {
    counter = test_server_->counter("thrift.thrift_stats.request_passthrough");
    EXPECT_EQ(1U, counter->value());
    counter = test_server_->counter("thrift.thrift_stats.response_passthrough");
    EXPECT_EQ(1U, counter->value());
  } else {
    counter = test_server_->counter("thrift.thrift_stats.request_passthrough");
    EXPECT_EQ(0U, counter->value());
    counter = test_server_->counter("thrift.thrift_stats.response_passthrough");
    EXPECT_EQ(0U, counter->value());
  }
  counter = test_server_->counter("thrift.thrift_stats.response_reply");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("thrift.thrift_stats.response_success");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("cluster.cluster_0.thrift.upstream_resp_reply");
  EXPECT_EQ(1U, counter->value());
  counter = test_server_->counter("cluster.cluster_0.thrift.upstream_resp_success");
  EXPECT_EQ(1U, counter->value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
