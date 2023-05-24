#include <memory>

#include "envoy/http/filter.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/matching/inputs.h"
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
  CONSTRUCT_ON_FIRST_USE(StreamInfo::StreamInfoImpl,
                         StreamInfo::StreamInfoImpl(Http::Protocol::Http2,
                                                    Event::GlobalTimeSystem().timeSystem(),
                                                    connectionInfoProvider()));
}

TEST(MatchingData, HttpRequestHeadersDataInput) {
  HttpRequestHeadersDataInput input("header");
  HttpMatchingDataImpl data(createStreamInfo());

  {
    TestRequestHeaderMapImpl request_headers({{"header", "bar"}});
    data.onRequestHeaders(request_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "bar");
  }

  {
    TestRequestHeaderMapImpl request_headers({{"not-header", "baz"}});
    data.onRequestHeaders(request_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpRequestTrailersDataInput) {
  HttpRequestTrailersDataInput input("header");
  HttpMatchingDataImpl data(createStreamInfo());

  {
    TestRequestTrailerMapImpl request_trailers({{"header", "bar"}});
    data.onRequestTrailers(request_trailers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "bar");
  }

  {
    TestRequestTrailerMapImpl request_trailers({{"not-header", "baz"}});
    data.onRequestTrailers(request_trailers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpResponseHeadersDataInput) {
  HttpResponseHeadersDataInput input("header");
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());

  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "bar");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpResponseTrailersDataInput) {
  HttpResponseTrailersDataInput input("header");
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());

  {
    TestResponseTrailerMapImpl response_trailers({{"header", "bar"}});
    data.onResponseTrailers(response_trailers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "bar");
  }

  {
    TestResponseTrailerMapImpl response_trailers({{"not-header", "baz"}});
    data.onResponseTrailers(response_trailers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

TEST(MatchingData, HttpRequestQueryParamsDataInput) {
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());

  {
    HttpRequestQueryParamsDataInput input("arg");
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    HttpRequestQueryParamsDataInput input("user name");
    TestRequestHeaderMapImpl request_headers({{":path", "/test?user%20name=foo%20bar"}});
    data.onRequestHeaders(request_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "foo bar");
  }

  {
    HttpRequestQueryParamsDataInput input("username");
    TestRequestHeaderMapImpl request_headers({{":path", "/test?username=fooA&username=fooB"}});
    data.onRequestHeaders(request_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "fooA");
  }

  {
    HttpRequestQueryParamsDataInput input("username");
    TestRequestHeaderMapImpl request_headers({{":path", "/test"}});
    data.onRequestHeaders(request_headers);

    const auto result = input.get(data);

    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }

  {
    HttpRequestQueryParamsDataInput input("username");
    TestRequestHeaderMapImpl request_headers;
    data.onRequestHeaders(request_headers);

    const auto result = input.get(data);

    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

} // namespace Matching
} // namespace Http
} // namespace Envoy
