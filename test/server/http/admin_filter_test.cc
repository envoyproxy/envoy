#include "server/http/admin.h"
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
      : admin_(TestEnvironment::temporaryPath("envoy.prof"), server_),
        filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  Stats::IsolatedStoreImpl listener_scope_;
  AdminImpl admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

// Check default implementations the admin class picks up.
TEST_P(AdminFilterTest, MiscFunctions) {
  EXPECT_EQ(false, admin_.preserveExternalRequestId());
  Http::MockFilterChainFactoryCallbacks mock_filter_chain_factory_callbacks;
  EXPECT_EQ(false,
            admin_.createUpgradeFilterChain("", nullptr, mock_filter_chain_factory_callbacks));
  EXPECT_TRUE(nullptr != admin_.scopedRouteConfigProvider());
  EXPECT_EQ(Http::ConnectionManagerConfig::HttpConnectionManagerProto::OVERWRITE,
            admin_.serverHeaderTransformation());
}

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
