#include "envoy/http/filter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/status_code_input.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

std::shared_ptr<Network::ConnectionInfoSetterImpl> connectionInfoProvider() {
  CONSTRUCT_ON_FIRST_USE(std::shared_ptr<Network::ConnectionInfoSetterImpl>,
                         std::make_shared<Network::ConnectionInfoSetterImpl>(
                             std::make_shared<Network::Address::Ipv4Instance>(80),
                             std::make_shared<Network::Address::Ipv4Instance>(80)));
}

StreamInfo::StreamInfoImpl createStreamInfo() {
  CONSTRUCT_ON_FIRST_USE(
      StreamInfo::StreamInfoImpl,
      StreamInfo::StreamInfoImpl(Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(),
                                 connectionInfoProvider(),
                                 StreamInfo::FilterState::LifeSpan::FilterChain));
}
TEST(MatchingData, HttpResponseStatusCodeInput) {
  HttpResponseStatusCodeInput input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());

  {
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::NotAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(200);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "200");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
}

TEST(MatchingData, HttpResponseStatusCodeClassInput) {
  HttpResponseStatusCodeClassInput input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());
  {
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::NotAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(100);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "1xx");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(200);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "2xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(300);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "3xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(400);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "4xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(500);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(input.get(data).stringData().value(), "5xx");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    response_headers.setStatus(600);
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    response_headers.setStatus(99);
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
    EXPECT_EQ(result.stringData(), absl::nullopt);
  }
}

TEST(MatchingData, HttpResponseLocalReplyInputNoDetails) {
  HttpResponseLocalReplyInput input;
  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connectionInfoProvider(),
      StreamInfo::FilterState::LifeSpan::FilterChain);
  HttpMatchingDataImpl data(stream_info);

  auto result = input.get(data);
  EXPECT_EQ(result.availability(), Matcher::DataAvailability::NotAvailable);
  EXPECT_EQ(result.stringData(), absl::nullopt);
}

TEST(MatchingData, HttpResponseLocalReplyInputUpstreamResponse) {
  HttpResponseLocalReplyInput input;
  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connectionInfoProvider(),
      StreamInfo::FilterState::LifeSpan::FilterChain);
  HttpMatchingDataImpl data(stream_info);

  stream_info.setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().ViaUpstream);
  auto result = input.get(data);
  EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
  EXPECT_EQ(result.stringData().value(), "false");
}

TEST(MatchingData, HttpResponseLocalReplyInputLocalReply) {
  HttpResponseLocalReplyInput input;
  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connectionInfoProvider(),
      StreamInfo::FilterState::LifeSpan::FilterChain);
  HttpMatchingDataImpl data(stream_info);

  stream_info.setResponseCodeDetails("route_not_found");
  auto result = input.get(data);
  EXPECT_EQ(result.availability(), Matcher::DataAvailability::AllDataAvailable);
  EXPECT_EQ(result.stringData().value(), "true");
}

TEST(MatchingData, HttpResponseLocalReplyInputVariousLocalDetails) {
  HttpResponseLocalReplyInput input;
  StreamInfo::StreamInfoImpl stream_info(
      Http::Protocol::Http2, Event::GlobalTimeSystem().timeSystem(), connectionInfoProvider(),
      StreamInfo::FilterState::LifeSpan::FilterChain);
  HttpMatchingDataImpl data(stream_info);

  // Verify multiple local reply detail strings are correctly identified.
  for (const auto& details :
       {"direct_response", "cluster_not_found", "maintenance_mode", "request_timeout"}) {
    stream_info.setResponseCodeDetails(details);
    auto result = input.get(data);
    EXPECT_EQ(result.stringData().value(), "true") << "Failed for details: " << details;
  }
}

} // namespace Matching
} // namespace Http
} // namespace Envoy
