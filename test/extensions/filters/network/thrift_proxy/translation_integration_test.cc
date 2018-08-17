#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"

#include "extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/network/thrift_proxy/integration.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/network_utility.h"

#include "absl/strings/ascii.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::TestParamInfo;
using testing::TestWithParam;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class ThriftTranslationIntegrationTest
    : public BaseThriftIntegrationTest,
      public TestWithParam<std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>> {
public:
  static void SetUpTestCase() {
    thrift_config_ = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        - name: envoy.filters.network.thrift_proxy
          config:
            stat_prefix: thrift_stats
            route_config:
              name: "routes"
              routes:
                - match:
                    method_name: "add"
                  route:
                    cluster: "cluster_0"
      )EOF";
  }

  void initialize() override {
    TransportType downstream_transport, upstream_transport;
    ProtocolType downstream_protocol, upstream_protocol;
    std::tie(downstream_transport, downstream_protocol, upstream_transport, upstream_protocol) =
        GetParam();

    auto upstream_transport_proto = transportTypeToProto(upstream_transport);
    auto upstream_protocol_proto = protocolTypeToProto(upstream_protocol);

    envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProtocolOptions proto_opts;
    proto_opts.set_transport(upstream_transport_proto);
    proto_opts.set_protocol(upstream_protocol_proto);

    ProtobufWkt::Struct struct_opts;
    MessageUtil::jsonConvert(proto_opts, struct_opts);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* opts = bootstrap.mutable_static_resources()
                       ->mutable_clusters(0)
                       ->mutable_extension_protocol_options();
      (*opts)[NetworkFilterNames::get().ThriftProxy] = struct_opts;
    });

    // Invent some varying, but deterministic, values to add. We use the add method instead of
    // execute because the default execute params contains a set and the ordering can vary across
    // generated payloads.
    std::vector<std::string> args({
        fmt::format("{}", (static_cast<int>(downstream_transport) << 8) +
                              static_cast<int>(downstream_protocol)),
        fmt::format("{}", (static_cast<int>(upstream_transport) << 8) +
                              static_cast<int>(upstream_protocol)),
    });

    PayloadOptions downstream_opts(downstream_transport, downstream_protocol, DriverMode::Success,
                                   {}, "add", args);
    preparePayloads(downstream_opts, downstream_request_bytes_, downstream_response_bytes_);

    PayloadOptions upstream_opts(upstream_transport, upstream_protocol, DriverMode::Success, {},
                                 "add", args);
    preparePayloads(upstream_opts, upstream_request_bytes_, upstream_response_bytes_);

    BaseThriftIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  Buffer::OwnedImpl downstream_request_bytes_;
  Buffer::OwnedImpl downstream_response_bytes_;
  Buffer::OwnedImpl upstream_request_bytes_;
  Buffer::OwnedImpl upstream_response_bytes_;
};

static std::string paramToString(
    const TestParamInfo<std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>>&
        params) {
  TransportType downstream_transport, upstream_transport;
  ProtocolType downstream_protocol, upstream_protocol;
  std::tie(downstream_transport, downstream_protocol, upstream_transport, upstream_protocol) =
      params.param;

  return fmt::format("From{}{}To{}{}", transportNameForTest(downstream_transport),
                     protocolNameForTest(downstream_protocol),
                     transportNameForTest(upstream_transport),
                     protocolNameForTest(upstream_protocol));
}

INSTANTIATE_TEST_CASE_P(
    TransportsAndProtocols, ThriftTranslationIntegrationTest,
    Combine(Values(TransportType::Framed, TransportType::Unframed, TransportType::Header),
            Values(ProtocolType::Binary, ProtocolType::Compact),
            Values(TransportType::Framed, TransportType::Unframed, TransportType::Header),
            Values(ProtocolType::Binary, ProtocolType::Compact)),
    paramToString);

// Tests that the proxy will translate between different downstream and upstream transports and
// protocols.
TEST_P(ThriftTranslationIntegrationTest, Translates) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->write(downstream_request_bytes_.toString());

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
  counter = test_server_->counter("thrift.thrift_stats.response_success");
  EXPECT_EQ(1U, counter->value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
