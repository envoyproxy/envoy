#include "source/common/common/utility.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/router/router_filter_interface.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

class UpstreamRequestTest : public testing::Test {
public:
  UpstreamRequestTest() : pool_(*symbol_table_) {
    HttpTestUtility::addDefaultHeaders(downstream_request_header_map_);
    ON_CALL(router_filter_interface_, downstreamHeaders())
        .WillByDefault(Return(&downstream_request_header_map_));
    ON_CALL(router_filter_interface_, timeSource).WillByDefault(ReturnRef(time_system));
  }

  Http::TestRequestHeaderMapImpl downstream_request_header_map_{};
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool_;
  NiceMock<MockTimeSystem> time_system;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockRouterFilterInterface> router_filter_interface_;
};

// UpstreamRequest is responsible processing for passing 101 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode101UpgradeHeaders) {
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};
  auto upgrade_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "101"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(upgrade_headers), false);
}

// UpstreamRequest is responsible for ignoring non-{100,101} 1xx headers.
TEST_F(UpstreamRequestTest, IgnoreOther1xxHeaders) {
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};
  auto other_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "102"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _)).Times(0);
  upstream_request_.decodeHeaders(std::move(other_headers), false);
}

// UpstreamRequest is responsible processing for passing 200 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode200UpgradeHeaders) {
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(response_headers), false);
}

// UpstreamRequest dumpState without allocating memory.
TEST_F(UpstreamRequestTest, DumpsStateWithoutAllocatingMemory) {
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};

  // Set up router filter
  auto connection_info_provider =
      router_filter_interface_.client_connection_.stream_info_.downstream_connection_info_provider_;
  connection_info_provider->setRemoteAddress(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));
  connection_info_provider->setLocalAddress(
      Network::Utility::parseInternetAddressAndPort("5.6.7.8:5678"));
  connection_info_provider->setDirectRemoteAddressForTest(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));

  // Dump State
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  Stats::TestUtil::MemoryTest memory_test;
  upstream_request_.dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check Contents
  EXPECT_THAT(ostream.contents(), HasSubstr("UpstreamRequest "));
  EXPECT_THAT(ostream.contents(), HasSubstr("addressProvider: \n  ConnectionInfoSetterImpl "));
  EXPECT_THAT(ostream.contents(), HasSubstr("request_headers: \n"));
}

// UpstreamRequest is responsible for adding proper gRPC annotations to spans.
TEST_F(UpstreamRequestTest, DecodeHeadersGrpcSpanAnnotations) {
  // Enable tracing in config.
  envoy::extensions::filters::http::router::v3::Router router_proto;
  router_proto.set_start_child_span(true);
  Router::FilterConfig config{pool_.add("prefix"), context_,
                              ShadowWriterPtr(new MockShadowWriter()), router_proto};
  EXPECT_CALL(router_filter_interface_, config()).WillRepeatedly(ReturnRef(config));

  // Set expectations on span.
  auto* child_span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(router_filter_interface_.callbacks_.active_span_, spawnChild_)
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag).Times(AnyNumber());
  EXPECT_CALL(*child_span, setTag(Eq("grpc.status_code"), Eq("1")));
  EXPECT_CALL(*child_span, setTag(Eq("grpc.message"), Eq("failure")));

  // System under test.
  UpstreamRequest upstream_request_{router_filter_interface_,
                                    std::make_unique<NiceMock<Router::MockGenericConnPool>>(),
                                    false, true};
  auto upgrade_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{":status", "200"}, {"grpc-status", "1"}, {"grpc-message", "failure"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_.decodeHeaders(std::move(upgrade_headers), false);
}

} // namespace
} // namespace Router
} // namespace Envoy
