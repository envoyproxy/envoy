#include "server/http/admin_filter.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Server {

class AdminFilterTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AdminFilterTest()
      : admin_server_callback_func_(createAdminServerCallback()),
        filter_(admin_server_callback_func_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  typedef std::function<Http::Code(absl::string_view path_and_query,
                                   Http::ResponseHeaderMap& response_headers,
                                   Buffer::OwnedImpl& response, AdminFilter& filter)>
      AdminServerCallbackFunction;

  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminServerCallbackFunction admin_server_callback_func_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;

  AdminServerCallbackFunction createAdminServerCallback() {
    return [](absl::string_view path_and_query, Http::ResponseHeaderMap& response_headers,
              Buffer::OwnedImpl& response, AdminFilter& filter) -> Http::Code {
      // silence compiler warnings for unused params
      (void)path_and_query;
      (void)response_headers;
      (void)filter;

      response.add("OK\n");
      return Http::Code::OK;
    };
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminFilterTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, true));
}

TEST_P(AdminFilterTest, Body) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_.decodeMetadata(metadata_map));
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, true));
}

TEST_P(AdminFilterTest, Trailers) {
  InSequence s;

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_.decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, addDecodedData(_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_.decodeData(data, false));
  EXPECT_CALL(callbacks_, decodingBuffer());
  filter_.getRequestBody();
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_.decodeTrailers(request_trailers));
}

} // namespace Server
} // namespace Envoy
