#include <memory>

#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/set_metadata/set_metadata_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ElementsAre;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

class SetMetadataIntegrationTest : public testing::Test {

public:
  SetMetadataIntegrationTest() {}

  void runFilter(envoy::config::core::v3::Metadata& metadata, const std::string& yaml_config,
                 Http::TestRequestHeaderMapImpl& headers) {
    envoy::extensions::filters::http::set_metadata::v3::Config ext_config;
    TestUtility::loadFromYaml(yaml_config, ext_config);
    auto config = std::make_shared<Config>(ext_config);
    auto filter = std::make_shared<SetMetadataFilter>(config);

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    filter->setDecoderFilterCallbacks(decoder_callbacks);

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(headers, true));
    filter->onDestroy();
  }

  void check_key_int(ProtobufWkt::Struct const& s, const char* key, int val) {
    auto& fields = s.fields();
    auto it = fields.find(key);
    ASSERT_TRUE(it != fields.end());
    auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), ProtobufWkt::Value::kNumberValue);
    EXPECT_EQ(pbval.number_value(), val);
  }
};

TEST_F(SetMetadataIntegrationTest, TestTagsHeaders) {
  Http::TestRequestHeaderMapImpl headers({
      {":method", "GET"},
      {":path", "/"},
  });

  const std::string yaml_config = R"EOF(
    metadata_namespace: thenamespace
    value:
      tags:
        mytag0: 1
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, headers);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 1}}}`
  auto& filter_metadata = metadata.filter_metadata();
  auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_TRUE(it_namespace != filter_metadata.end());
  auto& fields = it_namespace->second.fields();
  auto it_tags = fields.find("tags");
  ASSERT_TRUE(it_tags != fields.end());
  auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  check_key_int(tags.struct_value(), "mytag0", 1);
}

TEST_F(SetMetadataIntegrationTest, TestTagsHeadersUpdate) {
  envoy::config::core::v3::Metadata metadata;

  Http::TestRequestHeaderMapImpl headers({
      {":method", "GET"},
      {":path", "/"},
  });

  {
    const std::string yaml_config = R"EOF(
      metadata_namespace: thenamespace
      value:
        mynumber: 10
        mylist: ["a"]
        tags:
          mytag0: 1
    )EOF";

    runFilter(metadata, yaml_config, headers);
  }
  {
    const std::string yaml_config = R"EOF(
      metadata_namespace: thenamespace
      value:
        mynumber: 20
        mylist: ["b"]
        tags:
          mytag1: 1
    )EOF";

    runFilter(metadata, yaml_config, headers);
  }

  // Verify that `metadata` contains:
  // ``{"thenamespace": {number: 20, mylist: ["a","b"], "tags": {"mytag0": 1, "mytag1": 1}}}``
  auto& filter_metadata = metadata.filter_metadata();
  auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_TRUE(it_namespace != filter_metadata.end());
  auto& namespace_ = it_namespace->second;

  check_key_int(namespace_, "mynumber", 20);

  auto& fields = namespace_.fields();
  auto it_mylist = fields.find("mylist");
  ASSERT_TRUE(it_mylist != fields.end());
  auto& mylist = it_mylist->second;
  ASSERT_EQ(mylist.kind_case(), ProtobufWkt::Value::kListValue);
  auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  auto it_tags = fields.find("tags");
  ASSERT_TRUE(it_tags != fields.end());
  auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  auto& tags_struct = tags.struct_value();

  check_key_int(tags_struct, "mytag0", 1);
  check_key_int(tags_struct, "mytag1", 1);
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
