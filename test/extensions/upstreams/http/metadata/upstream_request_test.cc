#include <memory>

#include "extensions/upstreams/http/metadata/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Metadata {

class MetadataUpstreamTest : public ::testing::Test {
protected:
  Router::MockUpstreamToDownstream upstream_to_downstream_;
  ::testing::NiceMock<Envoy::Http::MockRequestEncoder> encoder_;
};

TEST_F(MetadataUpstreamTest, Basic) {
  Envoy::Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  auto upstream = std::make_unique<MetadataUpstream>(upstream_to_downstream_, &encoder_);
  EXPECT_TRUE(upstream->encodeHeaders(headers, false).ok());
}

} // namespace Metadata
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
