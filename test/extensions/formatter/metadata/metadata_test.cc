#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class MetadataFormatterTest : public ::testing::Test {
public:
  MetadataFormatterTest() : metadata_(std::make_shared<envoy::config::core::v3::Metadata>()) {
    // Create metadata object with test values.
    ProtobufWkt::Struct struct_obj;
    auto& fields_map = *struct_obj.mutable_fields();
    fields_map["test_key"] = ValueUtil::stringValue("test_value");
    (*metadata_->mutable_filter_metadata())["metadata.test"] = struct_obj;
  }

  // Method creates a yaml config for specific access log METADATA type.
  ::Envoy::Formatter::FormatterPtr getTestMetadataFormatter(std::string type,
                                                            std::string tag = "METADATA") {
    const std::string yaml = fmt::format(R"EOF(
  text_format_source:
    inline_string: "%{}({}:metadata.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF",
                                         tag, type);
    TestUtility::loadFromYaml(yaml, config_);
    return Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  }

  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::string body_;

  Envoy::Formatter::HttpFormatterContext formatter_context_{&request_headers_, &response_headers_,
                                                            &response_trailers_, body_};

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<envoy::config::core::v3::Metadata> metadata_;
};

// Exception should be thrown for tags different than METADATA.
TEST_F(MetadataFormatterTest, IncorrectTag) {
  EXPECT_THROW(getTestMetadataFormatter("ROUTE", "BLAH_BLAH"), EnvoyException);
}

// Exception should be thrown for unknown type of metadata.
TEST_F(MetadataFormatterTest, NonExistingMetadataProvider) {
  EXPECT_THROW(getTestMetadataFormatter("BLAH"), EnvoyException);
}

// Extensive testing of Dynamic Metadata formatter is in
// test/common/formatter/substitution_formatter_test.cc file.
// Here just make sure that METADATA(DYNAMIC .... returns
// Dynamic Metadata formatter and dynamicMetadata() is called.
TEST_F(MetadataFormatterTest, DynamicMetadata) {
  // Make sure that formatter accesses dynamic metadata.
  EXPECT_CALL(testing::Const(stream_info_), dynamicMetadata())
      .WillRepeatedly(testing::ReturnRef(*metadata_));

  EXPECT_EQ("test_value", getTestMetadataFormatter("DYNAMIC")->formatWithContext(formatter_context_,
                                                                                 stream_info_));
}

// Extensive testing of Cluster Metadata formatter is in
// test/common/formatter/substitution_formatter_test.cc file.
// Here just make sure that METADATA(CLUSTER .... accesses
// cluster's metadata object.
TEST_F(MetadataFormatterTest, ClusterMetadata) {
  // Make sure that formatter accesses cluster metadata.
  absl::optional<std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>> cluster =
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(**cluster, metadata()).WillRepeatedly(testing::ReturnRef(*metadata_));
  EXPECT_CALL(stream_info_, upstreamClusterInfo()).WillRepeatedly(testing::ReturnPointee(cluster));

  EXPECT_EQ("test_value", getTestMetadataFormatter("CLUSTER")->formatWithContext(formatter_context_,
                                                                                 stream_info_));
}

// Extensive testing of UpstreamHost Metadata formatter is in
// test/common/formatter/substitution_formatter_test.cc file.
// Here just make sure that METADATA(UPSTREAM_HOST .... accesses
// upstream host's's metadata object.
TEST_F(MetadataFormatterTest, UpstreamHostMetadata) {
  // Make sure that formatter accesses cluster metadata.
  // Get pointers to MockUpstreamInfo  and MockHostDescription.
  std::shared_ptr<StreamInfo::MockUpstreamInfo> mock_upstream_info =
      std::dynamic_pointer_cast<StreamInfo::MockUpstreamInfo>(stream_info_.upstreamInfo());
  std::shared_ptr<const Upstream::MockHostDescription> mock_host_description =
      std::dynamic_pointer_cast<const Upstream::MockHostDescription>(
          mock_upstream_info->upstreamHost());

  EXPECT_CALL(*mock_host_description, metadata()).WillRepeatedly(testing::Return(metadata_));

  EXPECT_EQ("test_value", getTestMetadataFormatter("UPSTREAM_HOST")
                              ->formatWithContext(formatter_context_, stream_info_));
}

// Test that METADATA(ROUTE accesses stream_info's Route.
TEST_F(MetadataFormatterTest, RouteMetadata) {
  std::shared_ptr<Router::MockRoute> route{new NiceMock<Router::MockRoute>()};
  EXPECT_CALL(*route, metadata()).WillRepeatedly(testing::ReturnRef(*metadata_));
  EXPECT_CALL(stream_info_, route()).WillRepeatedly(testing::Return(route));

  EXPECT_EQ("test_value",
            getTestMetadataFormatter("ROUTE")->formatWithContext(formatter_context_, stream_info_));
}

// Make sure that code handles nullptr returned for stream_info::route().
TEST_F(MetadataFormatterTest, NonExistentRouteMetadata) {
  EXPECT_CALL(stream_info_, route()).WillRepeatedly(testing::Return(nullptr));

  EXPECT_EQ("-",
            getTestMetadataFormatter("ROUTE")->formatWithContext(formatter_context_, stream_info_));
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
