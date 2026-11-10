#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

using testing::Contains;
using testing::Key;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

using FilterSharedPtr = std::shared_ptr<SetMetadataFilter>;

// Filter state object that serializes to a (non-Any) proto message.
class TestProtoFilterStateObject : public StreamInfo::FilterState::Object {
public:
  explicit TestProtoFilterStateObject(absl::string_view value) : value_(value) {}

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<envoy::extensions::filters::http::set_metadata::v3::Config>();
    message->set_metadata_namespace(value_);
    return message;
  }

private:
  const std::string value_;
};

// Filter state object that serializes directly to an Any.
class TestAnyFilterStateObject : public StreamInfo::FilterState::Object {
public:
  explicit TestAnyFilterStateObject(absl::string_view value) : value_(value) {}

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    envoy::extensions::filters::http::set_metadata::v3::Config message;
    message.set_metadata_namespace(value_);
    auto any = std::make_unique<Protobuf::Any>();
    any->PackFrom(message);
    return any;
  }

private:
  const std::string value_;
};

// Filter state object that only supports string serialization.
class TestStringFilterStateObject : public StreamInfo::FilterState::Object {
public:
  explicit TestStringFilterStateObject(absl::string_view value) : value_(value) {}

  std::optional<std::string> serializeAsString() const override { return value_; }

private:
  const std::string value_;
};

// Filter state object that supports no serialization at all.
class TestOpaqueFilterStateObject : public StreamInfo::FilterState::Object {};

class SetMetadataFilterTest : public testing::Test {

public:
  SetMetadataFilterTest() = default;

  void runFilter(envoy::config::core::v3::Metadata& metadata, const std::string& yaml_config,
                 StreamInfo::FilterStateSharedPtr filter_state = nullptr) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);
    config_ = std::make_shared<Config>(proto_config, *stats_store_.rootScope(), "");
    filter_ = std::make_shared<SetMetadataFilter>(config_);

    Http::TestRequestHeaderMapImpl headers;

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    if (filter_state != nullptr) {
      req_info.filter_state_ = std::move(filter_state);
    }
    filter_->setDecoderFilterCallbacks(decoder_callbacks);

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
    Buffer::OwnedImpl buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
    filter_->onDestroy();
  }

  void checkKeyInt(const Protobuf::Struct& s, std::string key, int val) {
    const auto& fields = s.fields();
    const auto it = fields.find(key);
    ASSERT_NE(it, fields.end());
    const auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), Protobuf::Value::kNumberValue);
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
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  ASSERT_EQ(mylist.kind_case(), Protobuf::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  ASSERT_EQ(mylist.kind_case(), Protobuf::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 2);
  ASSERT_EQ(vals[0].kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");
  ASSERT_EQ(vals[1].kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(vals[1].string_value(), "b");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
  const auto& tags_struct = tags.struct_value();

  checkKeyInt(tags_struct, "mytag0", 1);
  checkKeyInt(tags_struct, "mytag1", 1);
}

TEST_F(SetMetadataFilterTest, UntypedCompleteOverwriteAtKey) {
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(R"EOF(
    filter_metadata:
      thenamespace:
        tags:
          mynumber: 10
          mylist: ["a"]
  )EOF",
                            metadata);

  // By default, `allow_overwrite: true` merges Struct and appends List values
  // via `StructUtil::update`. To completely overwrite an untyped metadata
  // Struct at a specified key to an empty Struct `{}`, first overwrite the key
  // with a different value kind (such as `null`) and then set the empty Struct.
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        tags: null
      allow_overwrite: true
    - metadata_namespace: thenamespace
      value:
        tags: {}
      allow_overwrite: true
  )EOF";

  runFilter(metadata, yaml_config);

  // Verify that `metadata` contains `{"thenamespace": {"tags": {}}}`.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
  EXPECT_TRUE(tags.struct_value().fields().empty());
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
  ASSERT_EQ(mylist.kind_case(), Protobuf::Value::kListValue);
  const auto& vals = mylist.list_value().values();
  ASSERT_EQ(vals.size(), 1);
  ASSERT_EQ(vals[0].kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(vals[0].string_value(), "a");

  const auto it_tags = fields.find("tags");
  ASSERT_NE(it_tags, fields.end());
  const auto& tags = it_tags->second;
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  EXPECT_THAT(test_cfg.value().fields(), Contains(Key("bat")));
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
  EXPECT_THAT(test_cfg.value().fields(), Contains(Key("foo")));
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
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  ASSERT_EQ(tags.kind_case(), Protobuf::Value::kStructValue);
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
  EXPECT_LOG_CONTAINS("warn",
                      "set_metadata filter configuration contains metadata entries without value, "
                      "typed_value or filter_state",
                      runFilter(metadata, yaml_config));
}

TEST_F(SetMetadataFilterTest, FilterStateProtoObject) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey",
                        std::make_shared<TestProtoFilterStateObject>("from_filter_state"));

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  // Verify that the proto returned by the filter state object was packed into typed metadata.
  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace);
  const auto& any_val = it_namespace->second;
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config unpacked;
  ASSERT_TRUE(any_val.UnpackTo(&unpacked));
  EXPECT_EQ("from_filter_state", unpacked.metadata_namespace());

  // Untyped metadata must be untouched.
  EXPECT_TRUE(metadata.filter_metadata().empty());
  EXPECT_EQ(0, config_->stats().filter_state_not_found_.value());
  EXPECT_EQ(0, config_->stats().filter_state_not_serializable_.value());
  EXPECT_EQ(0, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, FilterStateAnyObjectIsNotDoubleWrapped) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey", std::make_shared<TestAnyFilterStateObject>("from_any"));

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace);
  const auto& any_val = it_namespace->second;
  // The Any produced by the object is used as is (type URL is the inner message, not Any).
  ASSERT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config",
            any_val.type_url());
  envoy::extensions::filters::http::set_metadata::v3::Config unpacked;
  ASSERT_TRUE(any_val.UnpackTo(&unpacked));
  EXPECT_EQ("from_any", unpacked.metadata_namespace());
}

TEST_F(SetMetadataFilterTest, FilterStateStringObject) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey", std::make_shared<TestStringFilterStateObject>("hello"));

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace);
  const auto& any_val = it_namespace->second;
  ASSERT_EQ("type.googleapis.com/google.protobuf.StringValue", any_val.type_url());
  Protobuf::StringValue unpacked;
  ASSERT_TRUE(any_val.UnpackTo(&unpacked));
  EXPECT_EQ("hello", unpacked.value());
}

TEST_F(SetMetadataFilterTest, FilterStateNotFound) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      filter_state:
        key: missing
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  EXPECT_TRUE(metadata.typed_filter_metadata().empty());
  EXPECT_TRUE(metadata.filter_metadata().empty());
  EXPECT_EQ(1, config_->stats().filter_state_not_found_.value());
  EXPECT_EQ(0, config_->stats().filter_state_not_serializable_.value());
}

TEST_F(SetMetadataFilterTest, FilterStateNotSerializable) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey", std::make_shared<TestOpaqueFilterStateObject>());

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  EXPECT_TRUE(metadata.typed_filter_metadata().empty());
  EXPECT_EQ(0, config_->stats().filter_state_not_found_.value());
  EXPECT_EQ(1, config_->stats().filter_state_not_serializable_.value());
}

TEST_F(SetMetadataFilterTest, FilterStateWithAllowOverwrite) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: static_namespace
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
      allow_overwrite: true
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey",
                        std::make_shared<TestProtoFilterStateObject>("from_filter_state"));

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace);
  envoy::extensions::filters::http::set_metadata::v3::Config unpacked;
  ASSERT_TRUE(it_namespace->second.UnpackTo(&unpacked));
  EXPECT_EQ("from_filter_state", unpacked.metadata_namespace());
  EXPECT_EQ(0, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, FilterStateWithNoAllowOverwrite) {
  const std::string yaml_config = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      typed_value:
        '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
        metadata_namespace: static_namespace
    - metadata_namespace: thenamespace
      filter_state:
        key: thekey
  )EOF";

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  filter_state->setData("thekey",
                        std::make_shared<TestProtoFilterStateObject>("from_filter_state"));

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config, filter_state);

  const auto& typed_metadata = metadata.typed_filter_metadata();
  const auto it_namespace = typed_metadata.find("thenamespace");
  ASSERT_NE(typed_metadata.end(), it_namespace);
  envoy::extensions::filters::http::set_metadata::v3::Config unpacked;
  ASSERT_TRUE(it_namespace->second.UnpackTo(&unpacked));
  EXPECT_EQ("static_namespace", unpacked.metadata_namespace());
  EXPECT_EQ(1, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, RouteSpecificConfigOverride) {
  const std::string global_yaml = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        global_key: global_val
  )EOF";

  const std::string route_yaml = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        route_key: route_val
      allow_overwrite: true
  )EOF";

  envoy::extensions::filters::http::set_metadata::v3::Config proto_global_config;
  TestUtility::loadFromYaml(global_yaml, proto_global_config);
  NiceMock<Stats::MockIsolatedStatsStore> local_stats_store;
  config_ = std::make_shared<Config>(proto_global_config, *local_stats_store.rootScope(), "");
  filter_ = std::make_shared<SetMetadataFilter>(config_);

  envoy::extensions::filters::http::set_metadata::v3::Config proto_route_config;
  TestUtility::loadFromYaml(route_yaml, proto_route_config);
  auto route_config =
      std::make_shared<Config>(proto_route_config, *local_stats_store.rootScope(), "");

  Http::TestRequestHeaderMapImpl headers;

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
  filter_->setDecoderFilterCallbacks(decoder_callbacks);

  EXPECT_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
      .WillRepeatedly(testing::Return(route_config.get()));

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
  EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();

  const auto it_global = fields.find("global_key");
  ASSERT_NE(it_global, fields.end());
  EXPECT_EQ(it_global->second.string_value(), "global_val");

  const auto it_route = fields.find("route_key");
  ASSERT_NE(it_route, fields.end());
  EXPECT_EQ(it_route->second.string_value(), "route_val");
}

TEST_F(SetMetadataFilterTest, RouteSpecificConfigOverwriteDenied) {
  const std::string global_yaml = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        key: global_val
  )EOF";

  const std::string route_yaml = R"EOF(
    metadata:
    - metadata_namespace: thenamespace
      value:
        key: route_val
  )EOF";

  envoy::extensions::filters::http::set_metadata::v3::Config proto_global_config;
  TestUtility::loadFromYaml(global_yaml, proto_global_config);
  NiceMock<Stats::MockIsolatedStatsStore> local_stats_store;
  config_ = std::make_shared<Config>(proto_global_config, *local_stats_store.rootScope(), "");
  filter_ = std::make_shared<SetMetadataFilter>(config_);

  envoy::extensions::filters::http::set_metadata::v3::Config proto_route_config;
  TestUtility::loadFromYaml(route_yaml, proto_route_config);
  auto route_config =
      std::make_shared<Config>(proto_route_config, *local_stats_store.rootScope(), "");

  Http::TestRequestHeaderMapImpl headers;

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
  filter_->setDecoderFilterCallbacks(decoder_callbacks);

  EXPECT_CALL(decoder_callbacks, mostSpecificPerFilterConfig())
      .WillRepeatedly(testing::Return(route_config.get()));

  envoy::config::core::v3::Metadata metadata;
  EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
  EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("thenamespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  const auto& fields = it_namespace->second.fields();

  const auto it_global = fields.find("key");
  ASSERT_NE(it_global, fields.end());
  EXPECT_EQ(it_global->second.string_value(), "global_val");

  EXPECT_EQ(1, route_config->stats().overwrite_denied_.value());
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
