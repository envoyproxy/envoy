#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.h"

#include "common/network/application_protocol.h"
#include "common/network/socket_option_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/http/alpn/alpn_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {
namespace {

class AlpnFilterTest : public testing::Test {
public:
  std::unique_ptr<AlpnFilter> makeDefaultFilter() {
    auto default_config = std::make_shared<AlpnFilterConfig>();
    auto filter = std::make_unique<AlpnFilter>(default_config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  std::unique_ptr<AlpnFilter> makeAlpnOverrideFilter(AlpnOverride alpn) {
    envoy::config::filter::http::alpn::v2alpha::FilterConfig proto_config;
    for (const auto& p : alpn) {
      envoy::config::filter::http::alpn::v2alpha::FilterConfig_Entry entry;
      entry.set_downstream_protocol(getProtocol(p.first));
      for (const auto& v : p.second) {
        entry.add_alpn(v);
      }
      proto_config.mutable_alpn_override()->Add(std::move(entry));
    }

    auto config = std::make_shared<AlpnFilterConfig>(proto_config);
    auto filter = std::make_unique<AlpnFilter>(config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

protected:
  envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol
  getProtocol(Http::Protocol protocol) {
    switch (protocol) {
    case Http::Protocol::Http10:
      return envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
          FilterConfig_Protocol_HTTP10;
    case Http::Protocol::Http11:
      return envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
          FilterConfig_Protocol_HTTP11;
    case Http::Protocol::Http2:
      return envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol::
          FilterConfig_Protocol_HTTP2;
    default:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl headers_;
};

TEST_F(AlpnFilterTest, NoDownstreamProtocol) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  auto filter = makeDefaultFilter();
  EXPECT_CALL(stream_info, filterState()).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

TEST_F(AlpnFilterTest, NoAlpnOverride) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  auto filter = makeDefaultFilter();
  EXPECT_CALL(stream_info, protocol()).WillOnce(Return(Http::Protocol::Http10));
  EXPECT_CALL(stream_info, filterState()).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

TEST_F(AlpnFilterTest, NoMatchedAlpnOverride) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  AlpnOverride alpn = {{Http::Protocol::Http11, {"foo", "bar"}}};
  auto filter = makeAlpnOverrideFilter(alpn);
  EXPECT_CALL(stream_info, protocol()).WillOnce(Return(Http::Protocol::Http10));
  EXPECT_CALL(stream_info, filterState()).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

TEST_F(AlpnFilterTest, OverrideAlpn) {

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  AlpnOverride alpn = {{Http::Protocol::Http10, {"foo", "bar"}},
                       {Http::Protocol::Http11, {"baz"}},
                       {Http::Protocol::Http2, {"qux"}}};
  auto filter = makeAlpnOverrideFilter(alpn);

  auto protocols = {Http::Protocol::Http10, Http::Protocol::Http11, Http::Protocol::Http2};
  for (const auto p : protocols) {
    EXPECT_CALL(stream_info, protocol()).WillOnce(Return(p));

    Envoy::StreamInfo::FilterStateImpl filter_state;
    EXPECT_CALL(stream_info, filterState()).WillOnce(ReturnRef(filter_state));
    EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_TRUE(
        filter_state.hasData<Network::ApplicationProtocols>(Network::ApplicationProtocols::key()));
    auto alpn_override =
        filter_state
            .getDataReadOnly<Network::ApplicationProtocols>(Network::ApplicationProtocols::key())
            .value();

    EXPECT_EQ(alpn_override, alpn[p]);
  }
}

} // namespace
} // namespace Alpn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
