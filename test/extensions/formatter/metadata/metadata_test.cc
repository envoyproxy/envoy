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

#if 0
void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata) {
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  ProtobufWkt::Struct struct_inner;
  (*struct_inner.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = struct_inner;
  fields_map["test_obj"] = val;
  (*metadata.mutable_filter_metadata())["dynamic.test"] = struct_obj;
}

void verifyStructOutput(ProtobufWkt::Struct output,
                        absl::node_hash_map<std::string, std::string> expected_map) {
  for (const auto& pair : expected_map) {
    EXPECT_EQ(output.fields().at(pair.first).string_value(), pair.second);
  }
}

TEST(SubstitutionFormatterTest, DISABLED_StructFormatterDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};
  std::string body;

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));
  EXPECT_CALL(testing::Const(stream_info), dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata));

  absl::node_hash_map<std::string, std::string> expected_json_map = {
      {"test_key", "test_value"},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "inner_value"}};

  ProtobufWkt::Struct key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    test_key: '%DYNAMIC_METADATA(com.test:test_key)%'
    test_obj: '%DYNAMIC_METADATA(com.test:test_obj)%'
    test_obj.inner_key: '%DYNAMIC_METADATA(com.test:test_obj:inner_key)%'
  )EOF",
                            key_mapping);
  ::Envoy::Formatter::StructFormatter formatter(key_mapping, false, false);


  verifyStructOutput(
      formatter.format(request_header, response_header, response_trailer, stream_info, body),
      expected_json_map);

}
#endif

class MetadataFormatterTest : public ::testing::Test {
public:
  MetadataFormatterTest() {
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  (*metadata_.mutable_filter_metadata())["dynamic.test"] = struct_obj;

  }

  ::Envoy::Formatter::FormatterPtr getTestMetadataFormatter(std::string type) {
  const std::string yaml = fmt::format(R"EOF(
  text_format_source:
    inline_string: "%METADATA({}:dynamic.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF",
  type);
  TestUtility::loadFromYaml(yaml, config_);
  return 
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"},
      {":path", "/request/path?secret=parameter"},
      {"x-envoy-original-path", "/original/path?secret=parameter"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  StreamInfo::MockStreamInfo stream_info_;
  std::string body_;

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  envoy::config::core::v3::Metadata metadata_;
};

TEST_F(MetadataFormatterTest, NonExistingMetadataProvider) {
  EXPECT_THROW(getTestMetadataFormatter("BLAH"), EnvoyException);
}


// Full and extensive testing of Dynamic Metadata formatter is in 
// test/common/formatter/substitution_formatter_test.cc file.
// Here just make sure that METADATA(DYNAMIC .... returns
// Dynamic Metadata formatter and run simple test.
TEST_F(MetadataFormatterTest, DynamicMetadata) {
#if 0
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%METADATA(DYNAMIC:dynamic.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
#endif

  // Make sure that formatter accesses dynamic metadata.
  //EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata_));
  EXPECT_CALL(testing::Const(stream_info_), dynamicMetadata()).WillRepeatedly(testing::ReturnRef(metadata_));

#if 0
  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
#endif

  EXPECT_EQ("test_value",
  getTestMetadataFormatter("DYNAMIC")->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));

}

TEST_F(MetadataFormatterTest, ClusterMetadata) {
#if 0
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%METADATA(CLUSTER:dynamic.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
#endif

  // Make sure that formatter accesses dynamic metadata.
  absl::optional<std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>> cluster =
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(**cluster, metadata()).WillRepeatedly(testing::ReturnRef(metadata_));
  EXPECT_CALL(stream_info_, upstreamClusterInfo()).WillRepeatedly(testing::ReturnPointee(cluster));
//  EXPECT_CALL(testing::Const(stream_info_), upstreamClusterInfo()).WillRepeatedly(testing::ReturnPointee(cluster));

#if 0
  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
#endif

  EXPECT_EQ("test_value",
  getTestMetadataFormatter("CLUSTER")->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));

}

TEST_F(MetadataFormatterTest, RouteMetadata) {
#if 0
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%METADATA(ROUTE:dynamic.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  (*metadata.mutable_filter_metadata())["dynamic.test"] = struct_obj;
#endif
  // Make sure that formatter accesses dynamic metadata.
  NiceMock<Router::MockRouteEntry> route;
  EXPECT_CALL(route, metadata()).WillRepeatedly(testing::ReturnRef(metadata_));
  EXPECT_CALL(stream_info_, routeEntry()).WillRepeatedly(testing::Return(&route));
  //EXPECT_CALL(testing::Const(stream_info_), routeEntry()).WillRepeatedly(testing::Return(&route));
#if 0

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
#endif

  EXPECT_EQ("test_value",
  getTestMetadataFormatter("ROUTE")->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));

}

TEST_F(MetadataFormatterTest, NonExistentRouteMetadata) {
#if 0
  const std::string yaml = R"EOF(
  text_format_source:
    inline_string: "%METADATA(ROUTE:dynamic.test:test_key)%"
  formatters:
    - name: envoy.formatter.metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.formatter.metadata.v3.Metadata
)EOF";
  TestUtility::loadFromYaml(yaml, config_);
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  (*metadata.mutable_filter_metadata())["dynamic.test"] = struct_obj;
#endif

  // Make sure that formatter accesses dynamic metadata.
  NiceMock<Router::MockRouteEntry> route;
  EXPECT_CALL(route, metadata()).WillRepeatedly(testing::ReturnRef(metadata_));
  EXPECT_CALL(stream_info_, routeEntry()).WillRepeatedly(testing::Return(nullptr));
  //EXPECT_CALL(testing::Const(stream_info_), routeEntry()).WillRepeatedly(testing::Return(&route));
#if 0

  auto formatter =
      Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_);
#endif

  EXPECT_EQ("-",
  getTestMetadataFormatter("ROUTE")->format(request_headers_, response_headers_,
                                               response_trailers_, stream_info_, body_));

}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
