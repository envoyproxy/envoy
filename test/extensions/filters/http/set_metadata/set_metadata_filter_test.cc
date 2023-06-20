#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

class SetMetadataIntegrationTest : public testing::Test {

public:
  SetMetadataIntegrationTest() = default;

  void runFilter(envoy::config::core::v3::Metadata& metadata, const std::string& yaml_config) {
    envoy::extensions::filters::http::set_metadata::v3::Config ext_config;
    TestUtility::loadFromYaml(yaml_config, ext_config);
    auto config = std::make_shared<Config>(ext_config);
    auto filter = std::make_shared<SetMetadataFilter>(config);

    Http::TestRequestHeaderMapImpl headers;

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    filter->setDecoderFilterCallbacks(decoder_callbacks);

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(headers, true));
    Buffer::OwnedImpl buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter->decodeData(buffer, true));
    filter->onDestroy();
  }

  void checkKeyInt(const ProtobufWkt::Struct& s, const char* key, int val) {
    const auto& fields = s.fields();
    const auto it = fields.find(key);
    ASSERT_TRUE(it != fields.end());
    const auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), ProtobufWkt::Value::kNumberValue);
    EXPECT_EQ(pbval.number_value(), val);
  }
};

TEST_F(SetMetadataIntegrationTest, TestTagsHeaders) {
  const std::string yaml_config = R"EOF(
    metadata_namespace: thenamespace
    value:
      tags:
        mytag0: 1
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 1}}}`
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_TRUE(it_namespace != filter_metadata.end());
  const auto& fields = it_namespace->second.fields();
  const auto it_tags = fields.find("tags");
  ASSERT_TRUE(it_tags != fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  checkKeyInt(tags.struct_value(), "mytag0", 1);
}

TEST_F(SetMetadataIntegrationTest, TestTagsHeadersUpdate) {
  envoy::config::core::v3::Metadata metadata;

  {
    const std::string yaml_config = R"EOF(
      metadata_namespace: thenamespace
      value:
        mynumber: 10
        mylist: ["a"]
        tags:
          mytag0: 1
    )EOF";

    runFilter(metadata, yaml_config);
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

    runFilter(metadata, yaml_config);
  }

  // Verify that `metadata` contains:
  // ``{"thenamespace": {number: 20, mylist: ["a","b"], "tags": {"mytag0": 1, "mytag1": 1}}}``
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_TRUE(it_namespace != filter_metadata.end());
  const auto& namespace_ = it_namespace->second;

  checkKeyInt(namespace_, "mynumber", 20);

  const auto& fields = namespace_.fields();
  const auto it_mylist = fields.find("mylist");
  ASSERT_TRUE(it_mylist != fields.end());
  const auto& mylist = it_mylist->second;
  ASSERT_EQ(mylist.kind_case(), ProtobufWkt::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  const auto it_tags = fields.find("tags");
  ASSERT_TRUE(it_tags != fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  const auto& tags_struct = tags.struct_value();

  checkKeyInt(tags_struct, "mytag0", 1);
  checkKeyInt(tags_struct, "mytag1", 1);
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
