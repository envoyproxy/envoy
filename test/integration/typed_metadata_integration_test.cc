#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using ListenerTypedMetadataIntegrationTest = ::Envoy::HttpProtocolIntegrationTest;

INSTANTIATE_TEST_SUITE_P(Protocols, ListenerTypedMetadataIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ListenerTypedMetadataIntegrationTest, Hello) {
  // Add some typed metadata to the listener.
  ProtobufWkt::StringValue value;
  value.set_value("hello world");
  ProtobufWkt::Any packed_value;
  packed_value.PackFrom(value);
  config_helper_.addListenerTypedMetadata("test.listener.typed.metadata", packed_value);

  // Add the filter that reads the listener typed metadata.
  config_helper_.addFilter(R"EOF({
    name: listener-typed-metadata-filter
  })EOF");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Main assertion on parsing the typed metadata is in the filter.
  // Here we just ensure the filter was created (so we know those assertions ran).
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Envoy
