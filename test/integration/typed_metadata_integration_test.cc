#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/integration.h"
#include "test/integration/typed_metadata_integration_test.pb.h"
#include "test/integration/typed_metadata_integration_test.pb.validate.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"
#include "test/test_common/registry.h"
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

class MockAccessLog : public AccessLog::Instance {
public:
  MOCK_METHOD(void, log,
              (const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*,
               const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo&));
};

class TestAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr createAccessLogInstance(
      const Protobuf::Message&, AccessLog::FilterPtr&&,
      Server::Configuration::ListenerAccessLogFactoryContext& context) override {
    // Check that expected listener metadata is present
    EXPECT_EQ(1, context.listenerMetadata().typed_filter_metadata().size());
    const auto iter =
        context.listenerMetadata().typed_filter_metadata().find("test.listener.typed.metadata");
    EXPECT_NE(iter, context.listenerMetadata().typed_filter_metadata().end());
    return std::make_shared<NiceMock<MockAccessLog>>();
  }

  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message&, AccessLog::FilterPtr&&,
                          Server::Configuration::CommonFactoryContext&) override {
    // This method should never be called in this test
    ASSERT(false);
    return nullptr;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new test::integration::typed_metadata::TestAccessLog()};
  }

  std::string name() const override { return "envoy.access_loggers.test"; }
};

// Validate that access logger gets the right context with access to listener metadata
TEST_P(ListenerTypedMetadataIntegrationTest, ListenerMetadataPlumbingToAccessLog) {
  TestAccessLogFactory factory;
  Registry::InjectFactory<Server::Configuration::AccessLogInstanceFactory> factory_register(
      factory);

  // Add some typed metadata to the listener.
  ProtobufWkt::StringValue value;
  value.set_value("hello world");
  ProtobufWkt::Any packed_value;
  packed_value.PackFrom(value);
  config_helper_.addListenerTypedMetadata("test.listener.typed.metadata", packed_value);

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        test::integration::typed_metadata::TestAccessLog access_log_config;
        hcm.mutable_access_log(0)->mutable_typed_config()->PackFrom(access_log_config);
      });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Envoy
