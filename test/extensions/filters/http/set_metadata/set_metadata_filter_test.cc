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

using FilterSharedPtr = std::shared_ptr<SetMetadataFilter>;

class SetMetadataFilterTest : public testing::Test {

public:
  SetMetadataFilterTest() = default;

  void runFilter(envoy::config::core::v3::Metadata& metadata, const std::string& yaml_config) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);
    config_ = std::make_shared<Config>(proto_config, *stats_store_.rootScope(), "");
    filter_ = std::make_shared<SetMetadataFilter>(config_);

    Http::TestRequestHeaderMapImpl headers;

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    filter_->setDecoderFilterCallbacks(decoder_callbacks);

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
    Buffer::OwnedImpl buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
    filter_->onDestroy();
  }

  void checkKeyInt(const ProtobufWkt::Struct& s, std::string key, int val) {
    const auto& fields = s.fields();
    const auto it = fields.find(key);
    ASSERT_NE(it, fields.end());
    const auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), ProtobufWkt::Value::kNumberValue);
    EXPECT_EQ(pbval.number_value(), val);
  }

  ConfigSharedPtr config_;
  FilterSharedPtr filter_;

private:
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

TEST_F(SetMetadataFilterTest, DeprecatedSimple) {
  const std::string yaml_config = R"EOF(
    metadata_namespace: thenamespace
    value:
      tags:
        mytag0: 1
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 1}}}`.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();
  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  checkKeyInt(tags.struct_value(), "mytag0", 1);
}

TEST_F(SetMetadataFilterTest, DeprecatedWithMerge) {
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
  // ``{"thenamespace": {number: 20, mylist: ["a","b"], "tags": {"mytag0": 1, "mytag1": 1}}}``.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& namespaced_md = it_namespace->second;

  checkKeyInt(namespaced_md, "mynumber", 20);

  const auto& fields = namespaced_md.fields();
  const auto it_mylist = fields.find("mylist");
  ASSERT_NE(it_mylist, fields.end());
  const auto& mylist = it_mylist->second;
  ASSERT_EQ(mylist.kind_case(), ProtobufWkt::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  const auto& tags_struct = tags.struct_value();

  checkKeyInt(tags_struct, "mytag0", 1);
  checkKeyInt(tags_struct, "mytag1", 1);
}

TEST_F(SetMetadataFilterTest, UntypedSimple) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        tags:
          mytag0: 1
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 1}}}`.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();
  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  checkKeyInt(tags.struct_value(), "mytag0", 1);
}

TEST_F(SetMetadataFilterTest, TypedSimple) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: foo_namespace
        value:
          foo: bar
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains our typed Config.
  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace2 = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace2);
  const auto any_val = it_namespace2->second;
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config test_cfg;
  ASSERT_TRUE(any_val.UnpackTo(&test_cfg));
  EXPECT_EQ("foo_namespace", test_cfg.metadata_namespace());
}

TEST_F(SetMetadataFilterTest, UntypedWithAllowOverwrite) {
  envoy::config::core::v3::Metadata metadata;

  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        mynumber: 10
        mylist: ["a"]
        tags:
          mytag0: 1
    - metadata_namespace: thenamespace
      value:
        mynumber: 20
        mylist: ["b"]
        tags:
          mytag1: 1
      allow_overwrite: true
  )EOF";

  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains:
  // ``{"thenamespace": {number: 20, mylist: ["a","b"], "tags": {"mytag0": 1, "mytag1": 1}}}``.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& namespaced_md = it_namespace->second;

  checkKeyInt(namespaced_md, "mynumber", 20);

  const auto& fields = namespaced_md.fields();
  const auto it_mylist = fields.find("mylist");
  ASSERT_NE(it_mylist, fields.end());
  const auto& mylist = it_mylist->second;
  ASSERT_EQ(mylist.kind_case(), ProtobufWkt::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  const auto& tags_struct = tags.struct_value();

  checkKeyInt(tags_struct, "mytag0", 1);
  checkKeyInt(tags_struct, "mytag1", 1);
}

TEST_F(SetMetadataFilterTest, UntypedWithNoAllowOverwrite) {
  envoy::config::core::v3::Metadata metadata;

  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        mynumber: 10
        mylist: ["a"]
        tags:
          mytag0: 1
    - metadata_namespace: thenamespace
      value:
        mynumber: 20
        mylist: ["b"]
        tags:
          mytag1: 1
  )EOF";

  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains:
  // ``{"thenamespace": {number: 10, mylist: ["a"], "tags": {"mytag0": 1}}}``.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& namespaced_md = it_namespace->second;

  checkKeyInt(namespaced_md, "mynumber", 10);

  const auto& fields = namespaced_md.fields();
  const auto it_mylist = fields.find("mylist");
  ASSERT_NE(it_mylist, fields.end());
  const auto& mylist = it_mylist->second;
  ASSERT_EQ(mylist.kind_case(), ProtobufWkt::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 1);
  ASSERT_EQ(vals[0].kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  const auto& tags_struct = tags.struct_value();

  checkKeyInt(tags_struct, "mytag0", 1);
  EXPECT_EQ(1, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, TypedWithAllowOverwrite) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: foo_namespace
        value:
          foo: bar
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: bat_namespace
        value:
          bat: baz
      allow_overwrite: true
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains our typed Config.
  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace2 = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace2);
  const auto any_val = it_namespace2->second;
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config test_cfg;
  ASSERT_TRUE(any_val.UnpackTo(&test_cfg));
  EXPECT_EQ("bat_namespace", test_cfg.metadata_namespace());
  ASSERT_TRUE(test_cfg.has_value());
  EXPECT_TRUE(test_cfg.value().fields().contains("bat"));
}

TEST_F(SetMetadataFilterTest, TypedWithNoAllowOverwrite) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: foo_namespace
        value:
          foo: bar
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: bat_namespace
        value:
          bat: baz
      allow_overwrite: false
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains our typed Config.
  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace2 = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace2);
  const auto any_val = it_namespace2->second;
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config test_cfg;
  ASSERT_TRUE(any_val.UnpackTo(&test_cfg));
  EXPECT_EQ("foo_namespace", test_cfg.metadata_namespace());
  ASSERT_TRUE(test_cfg.has_value());
  EXPECT_TRUE(test_cfg.value().fields().contains("foo"));
  EXPECT_EQ(1, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, UntypedWithDeprecated) {
  const std::string yaml_config = R"EOF(
    metadata_namespace: thenamespace
    value:
      tags:
        mytag0: 0
    metadata:
    - metadata_namespace: thenamespace
      value:
        tags:
          mytag0: 1
      allow_overwrite: true
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 1}}}`.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();
  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  checkKeyInt(tags.struct_value(), "mytag0", 1);
}

TEST_F(SetMetadataFilterTest, TypedWithDeprecated) {
  const std::string yaml_config = R"EOF(
    metadata_namespace: thenamespace
    value:
      tags:
        mytag0: 0
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: foo_namespace
        value:
          foo: bar
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {"mytag0": 0}}}`.
  const auto& untyped_metadata = metadata.filter_metadata();
  const auto it_namespace = untyped_metadata.find("thenamespace");
  ASSERT_NE(untyped_metadata.end(), it_namespace);
  const auto& fields = it_namespace->second.fields();
  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), ProtobufWkt::Value::kStructValue);
  checkKeyInt(tags.struct_value(), "mytag0", 0);

  // Verify that `metadata` contains our typed Config.
  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace2 = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace2);
  const auto any_val = it_namespace2->second;
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config test_cfg;
  ASSERT_TRUE(any_val.UnpackTo(&test_cfg));
  EXPECT_EQ("foo_namespace", test_cfg.metadata_namespace());
}

TEST_F(SetMetadataFilterTest, LogsErrorWhenNoValueConfigured) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  EXPECT_LOG_CONTAINS(
      "warn",
      "set_metadata filter configuration contains metadata entries without value or typed_value",
      runFilter(metadata, yaml_config));
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
