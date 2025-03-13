#include "envoy/http/filter.h"

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
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(200);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "200");
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

TEST(MatchingData, HttpResponseStatusCodeClassInput) {
  HttpResponseStatusCodeClassInput input;
  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  HttpMatchingDataImpl data(createStreamInfo());
  {
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::NotAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(100);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "1xx");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(200);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "2xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(300);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "3xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(400);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "4xx");
  }
  {
    TestResponseHeaderMapImpl response_headers({{"header", "bar"}});
    response_headers.setStatus(500);
    data.onResponseHeaders(response_headers);

    EXPECT_EQ(absl::get<std::string>(input.get(data).data_), "5xx");
  }

  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    response_headers.setStatus(600);
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
  {
    TestResponseHeaderMapImpl response_headers({{"not-header", "baz"}});
    response_headers.setStatus(99);
    data.onResponseHeaders(response_headers);
    auto result = input.get(data);
    EXPECT_EQ(result.data_availability_,
              Matcher::DataInputGetResult::DataAvailability::AllDataAvailable);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(result.data_));
  }
}

} // namespace Matching
} // namespace Http
} // namespace Envoy
