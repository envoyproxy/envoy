#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
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

    // Create GenericFactoryContext from mock FactoryContext.
    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    ASSERT_TRUE(config_or_error.ok());
    config_ = config_or_error.value();
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

  void runResponseFilter(envoy::config::core::v3::Metadata& metadata,
                         const std::string& yaml_config) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    // Create GenericFactoryContext from mock FactoryContext.
    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    ASSERT_TRUE(config_or_error.ok());
    config_ = config_or_error.value();
    filter_ = std::make_shared<SetMetadataFilter>(config_);

    Http::TestResponseHeaderMapImpl headers;

    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    filter_->setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(encoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
    Buffer::OwnedImpl buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
    filter_->onDestroy();
  }

  void runBiDirectionalFilter(envoy::config::core::v3::Metadata& metadata,
                              const std::string& yaml_config) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    // Create GenericFactoryContext from mock FactoryContext.
    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    ASSERT_TRUE(config_or_error.ok());
    config_ = config_or_error.value();
    filter_ = std::make_shared<SetMetadataFilter>(config_);

    Http::TestRequestHeaderMapImpl req_headers;
    Http::TestResponseHeaderMapImpl resp_headers;

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;

    filter_->setDecoderFilterCallbacks(decoder_callbacks);
    filter_->setEncoderFilterCallbacks(encoder_callbacks);

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(encoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

    // Process request
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(req_headers, true));
    Buffer::OwnedImpl req_buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(req_buffer, true));

    // Process response
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(resp_headers, true));
    Buffer::OwnedImpl resp_buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(resp_buffer, true));

    filter_->onDestroy();
  }

  void runFilterWithVirtualCluster(envoy::config::core::v3::Metadata& metadata,
                                   const std::string& yaml_config,
                                   const std::string& virtual_cluster_name) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    // Create GenericFactoryContext from mock FactoryContext.
    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    ASSERT_TRUE(config_or_error.ok());
    config_ = config_or_error.value();
    filter_ = std::make_shared<SetMetadataFilter>(config_);

    Http::TestRequestHeaderMapImpl headers;

    NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
    NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
    filter_->setDecoderFilterCallbacks(decoder_callbacks);

    // Mock virtual cluster name.
    virtual_cluster_name_ = absl::optional<std::string>(virtual_cluster_name);
    EXPECT_CALL(req_info, virtualClusterName())
        .WillRepeatedly(testing::ReturnRef(virtual_cluster_name_));

    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
    EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
    Buffer::OwnedImpl buffer;
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
    filter_->onDestroy();
  }

  void runFilterWithHeaders(envoy::config::core::v3::Metadata& metadata,
                            const std::string& yaml_config,
                            Http::TestRequestHeaderMapImpl& headers) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    // Create GenericFactoryContext from mock FactoryContext.
    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    ASSERT_TRUE(config_or_error.ok());
    config_ = config_or_error.value();
    filter_ = std::make_shared<SetMetadataFilter>(config_);

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

  void checkKeyString(const ProtobufWkt::Struct& s, std::string key, const std::string& val) {
    const auto& fields = s.fields();
    const auto it = fields.find(key);
    ASSERT_NE(it, fields.end());
    const auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), ProtobufWkt::Value::kStringValue);
    EXPECT_EQ(pbval.string_value(), val);
  }

  void checkKeyNumber(const ProtobufWkt::Struct& s, std::string key, double val) {
    const auto& fields = s.fields();
    const auto it = fields.find(key);
    ASSERT_NE(it, fields.end());
    const auto& pbval = it->second;
    ASSERT_EQ(pbval.kind_case(), ProtobufWkt::Value::kNumberValue);
    EXPECT_EQ(pbval.number_value(), val);
  }

  void expectConfigThrow(const std::string& yaml_config, const std::string& expected_error) {
    envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
    TestUtility::loadFromYaml(yaml_config, proto_config);

    Server::GenericFactoryContextImpl generic_context(factory_context_);
    auto config_or_error =
        Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
    EXPECT_FALSE(config_or_error.ok());
    EXPECT_THAT(std::string(config_or_error.status().message()), HasSubstr(expected_error));
  }

  ConfigSharedPtr config_;
  FilterSharedPtr filter_;

protected:
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

private:
  absl::optional<std::string> virtual_cluster_name_;
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
  EXPECT_LOG_CONTAINS("warn",
                      "set_metadata filter configuration contains metadata entries without value, "
                      "typed_value, or format_string",
                      runFilter(metadata, yaml_config));
}

TEST_F(SetMetadataFilterTest, FormatStringWithStaticText) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        allow_overwrite: true
        format_string:
          text_format_source:
            inline_string: "static_value"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify the static text was set correctly
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());

  // Check that the "value" field contains the formatted string.
  checkKeyString(it_namespace->second, "value", "static_value");
}

TEST_F(SetMetadataFilterTest, InvalidFormatString) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        allow_overwrite: true
        format_string:
          text_format_source:
            inline_string: "invalid: %INVALID_PLACEHOLDER%"
  )EOF";

  expectConfigThrow(yaml_config,
                    "Failed to create formatter for metadata namespace 'test_namespace': Not "
                    "supported field in StreamInfo: INVALID_PLACEHOLDER");
}

TEST_F(SetMetadataFilterTest, FormatStringMergeWithExistingMetadata) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        allow_overwrite: true
        format_string:
          text_format_source:
            inline_string: "dynamic_value"
  )EOF";

  // Pre-populate metadata.
  envoy::config::core::v3::Metadata metadata;
  auto& existing_metadata = *metadata.mutable_filter_metadata();
  ProtobufWkt::Struct existing_struct;
  (*existing_struct.mutable_fields())["static_field"].set_string_value("static_value");
  existing_metadata["test_namespace"] = existing_struct;

  runFilter(metadata, yaml_config);

  // Verify that both static and dynamic values exist after merge.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());

  checkKeyString(it_namespace->second, "static_field", "static_value");
  checkKeyString(it_namespace->second, "value", "dynamic_value");
}

TEST_F(SetMetadataFilterTest, FormatStringOverwriteBlocked) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        allow_overwrite: false
        format_string:
          text_format_source:
            inline_string: "new_value"
  )EOF";

  // Pre-populate metadata.
  envoy::config::core::v3::Metadata metadata;
  auto& existing_metadata = *metadata.mutable_filter_metadata();
  ProtobufWkt::Struct existing_struct;
  (*existing_struct.mutable_fields())["value"].set_string_value("existing_value");
  existing_metadata["test_namespace"] = existing_struct;

  runFilter(metadata, yaml_config);

  // Verify that the existing value is preserved and stats incremented.
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());

  checkKeyString(it_namespace->second, "value", "existing_value");

  // Verify stats were incremented.
  EXPECT_EQ(1UL, config_->stats().overwrite_denied_.value());
}

TEST_F(SetMetadataFilterTest, MixedMetadataTypes) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: untyped_namespace
        allow_overwrite: true
        value:
          static_field: "static_value"
      - metadata_namespace: formatted_namespace
        allow_overwrite: true
        format_string:
          text_format_source:
            inline_string: "simple_text"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify both types of metadata are set correctly.
  const auto& filter_metadata = metadata.filter_metadata();

  // Check untyped metadata.
  const auto it_untyped = filter_metadata.find("untyped_namespace");
  ASSERT_NE(it_untyped, filter_metadata.end());
  checkKeyString(it_untyped->second, "static_field", "static_value");

  // Check formatted metadata.
  const auto it_formatted = filter_metadata.find("formatted_namespace");
  ASSERT_NE(it_formatted, filter_metadata.end());
  checkKeyString(it_formatted->second, "value", "simple_text");
}

TEST_F(SetMetadataFilterTest, ApplyOnRequestOnly) {
  const std::string yaml_config = R"EOF(
    apply_on: REQUEST
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "request_value"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runBiDirectionalFilter(metadata, yaml_config);

  // Verify metadata was set (should only be set once on request)
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "request_value");
}

TEST_F(SetMetadataFilterTest, ApplyOnResponseOnly) {
  const std::string yaml_config = R"EOF(
    apply_on: RESPONSE
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "response_value"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runResponseFilter(metadata, yaml_config);

  // Verify metadata was set during response processing
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "response_value");
}

TEST_F(SetMetadataFilterTest, ApplyOnBoth) {
  const std::string yaml_config = R"EOF(
    apply_on: BOTH
    metadata:
      - metadata_namespace: test_namespace
        allow_overwrite: true
        format_string:
          text_format_source:
            inline_string: "both_value"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runBiDirectionalFilter(metadata, yaml_config);

  // Verify metadata was set (should be set on both request and response)
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "both_value");
}

TEST_F(SetMetadataFilterTest, BackwardCompatibilityDefaultApplyOn) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "default_value"
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Verify metadata was set (default should be REQUEST only)
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "default_value");
}

TEST_F(SetMetadataFilterTest, FormatStringWithStringType) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "string_value"
        type: STRING
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "string_value");
}

TEST_F(SetMetadataFilterTest, FormatStringWithNumberType) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "42.5"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyNumber(it_namespace->second, "value", 42.5);
}

TEST_F(SetMetadataFilterTest, FormatStringWithInvalidNumber) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "not_a_number"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_LOG_CONTAINS("debug", "Failed to convert 'not_a_number' to number",
                      runFilter(metadata, yaml_config));

  // Verify metadata was not set due to conversion failure
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  EXPECT_EQ(it_namespace, filter_metadata.end());
}

TEST_F(SetMetadataFilterTest, FormatStringWithBase64Encode) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "dGVzdF92YWx1ZQ=="  # "test_value" in base64
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "test_value");
}

TEST_F(SetMetadataFilterTest, FormatStringWithInvalidBase64) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "invalid_base64!"
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_LOG_CONTAINS("debug", "Base64 decode failed", runFilter(metadata, yaml_config));

  // Verify metadata was not set due to decode failure
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  EXPECT_EQ(it_namespace, filter_metadata.end());
}

TEST_F(SetMetadataFilterTest, ResponseOnlyWithNoRequestProcessing) {
  const std::string yaml_config = R"EOF(
    apply_on: RESPONSE
    metadata:
      - metadata_namespace: response_namespace
        format_string:
          text_format_source:
            inline_string: "response_only"
  )EOF";

  envoy::config::core::v3::Metadata metadata;

  // Run only request processing - should not set metadata
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("response_namespace");
  EXPECT_EQ(it_namespace, filter_metadata.end());
}

TEST_F(SetMetadataFilterTest, NumberTypeWithDecimalNumber) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "123.456"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyNumber(it_namespace->second, "value", 123.456);
}

TEST_F(SetMetadataFilterTest, NumberTypeWithIntegerNumber) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "42"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyNumber(it_namespace->second, "value", 42.0);
}

TEST_F(SetMetadataFilterTest, TypeAndEncodeWithStringValue) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "dGVzdA=="  # "test" in base64
        type: STRING
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "test");
}

TEST_F(SetMetadataFilterTest, TypeAndEncodeWithNumberValue) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "NDI="  # "42" in base64
        type: NUMBER
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyNumber(it_namespace->second, "value", 42.0);
}

TEST_F(SetMetadataFilterTest, MultipleFormattedMetadataWithDifferentTypes) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: string_namespace
        format_string:
          text_format_source:
            inline_string: "text_value"
        type: STRING
      - metadata_namespace: number_namespace
        format_string:
          text_format_source:
            inline_string: "123"
        type: NUMBER
      - metadata_namespace: encoded_namespace
        format_string:
          text_format_source:
            inline_string: "ZW5jb2RlZA=="  # "encoded" in base64
        type: STRING
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();

  // Check string metadata
  const auto it_string = filter_metadata.find("string_namespace");
  ASSERT_NE(it_string, filter_metadata.end());
  checkKeyString(it_string->second, "value", "text_value");

  // Check number metadata
  const auto it_number = filter_metadata.find("number_namespace");
  ASSERT_NE(it_number, filter_metadata.end());
  checkKeyNumber(it_number->second, "value", 123.0);

  // Check encoded metadata
  const auto it_encoded = filter_metadata.find("encoded_namespace");
  ASSERT_NE(it_encoded, filter_metadata.end());
  checkKeyString(it_encoded->second, "value", "encoded");
}

TEST_F(SetMetadataFilterTest, ProtobufValueType) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "\n\004test"  # Simple protobuf Value with string_value
        type: PROTOBUF_VALUE
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  EXPECT_LOG_CONTAINS("debug", "Failed to parse protobuf value", runFilter(metadata, yaml_config));

  // Verify metadata was not set due to parsing failure
  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  EXPECT_EQ(it_namespace, filter_metadata.end());
}

// Test edge case: Empty format string
TEST_F(SetMetadataFilterTest, EmptyFormatString) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: ""
        type: STRING
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();
  const auto it_namespace = filter_metadata.find("test_namespace");
  ASSERT_NE(it_namespace, filter_metadata.end());
  checkKeyString(it_namespace->second, "value", "");
}

// Test edge case: Configuration with no metadata entries
TEST_F(SetMetadataFilterTest, EmptyConfiguration) {
  const std::string yaml_config = R"EOF(
    apply_on: REQUEST
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  // Should not crash and metadata should remain empty
  const auto& filter_metadata = metadata.filter_metadata();
  EXPECT_TRUE(filter_metadata.empty());
}

// Test filter lifecycle - onDestroy method
TEST_F(SetMetadataFilterTest, FilterLifecycle) {
  const std::string yaml_config = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "test_value"
  )EOF";

  envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  Server::GenericFactoryContextImpl generic_context(factory_context_);
  auto config_or_error =
      Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  auto filter = std::make_shared<SetMetadataFilter>(config);

  // Test onDestroy - should not crash
  filter->onDestroy();
}

// Test all encoder filter methods for coverage
TEST_F(SetMetadataFilterTest, EncoderFilterMethods) {
  const std::string yaml_config = R"EOF(
    apply_on: RESPONSE
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "response_test"
  )EOF";

  envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  Server::GenericFactoryContextImpl generic_context(factory_context_);
  auto config_or_error =
      Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  auto filter = std::make_shared<SetMetadataFilter>(config);

  Http::TestResponseHeaderMapImpl headers;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info;
  envoy::config::core::v3::Metadata metadata;

  filter->setEncoderFilterCallbacks(encoder_callbacks);
  EXPECT_CALL(encoder_callbacks, streamInfo()).WillRepeatedly(ReturnRef(req_info));
  EXPECT_CALL(req_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  // Test all encoder filter methods
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter->encode1xxHeaders(headers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter->encodeData(buffer, false));

  Http::TestResponseTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter->encodeTrailers(trailers));

  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter->encodeMetadata(metadata_map));
}

// Test config accessor methods for coverage
TEST_F(SetMetadataFilterTest, ConfigAccessorMethods) {
  const std::string yaml_config = R"EOF(
    apply_on: BOTH
    metadata:
      - metadata_namespace: untyped_test
        value:
          key: "value"
      - metadata_namespace: typed_test
        typed_value:
          '@type': type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config
          metadata_namespace: nested
      - metadata_namespace: formatted_test
        format_string:
          text_format_source:
            inline_string: "formatted"
        type: STRING
  )EOF";

  envoy::extensions::filters::http::set_metadata::v3::Config proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  Server::GenericFactoryContextImpl generic_context(factory_context_);
  auto config_or_error =
      Config::create(proto_config, *stats_store_.rootScope(), "", generic_context);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();

  // Test all accessor methods
  EXPECT_FALSE(config->untyped().empty());
  EXPECT_FALSE(config->typed().empty());
  EXPECT_FALSE(config->formatted().empty());
  EXPECT_EQ(config->applyOn(), envoy::extensions::filters::http::set_metadata::v3::Config::BOTH);

  // Test stats accessor
  const auto& stats = config->stats();
  EXPECT_EQ(0UL, stats.overwrite_denied_.value());
}

// Test number conversion edge cases
TEST_F(SetMetadataFilterTest, NumberConversionEdgeCases) {
  // Test with whitespace around number
  const std::string yaml_config1 = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "  42.5  "
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata1;
  runFilter(metadata1, yaml_config1);

  const auto& filter_metadata1 = metadata1.filter_metadata();
  const auto it_namespace1 = filter_metadata1.find("test_namespace");
  ASSERT_NE(it_namespace1, filter_metadata1.end());
  checkKeyNumber(it_namespace1->second, "value", 42.5);

  // Test with negative number
  const std::string yaml_config2 = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "-123.456"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata2;
  runFilter(metadata2, yaml_config2);

  const auto& filter_metadata2 = metadata2.filter_metadata();
  const auto it_namespace2 = filter_metadata2.find("test_namespace");
  ASSERT_NE(it_namespace2, filter_metadata2.end());
  checkKeyNumber(it_namespace2->second, "value", -123.456);

  // Test with zero
  const std::string yaml_config3 = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "0"
        type: NUMBER
  )EOF";

  envoy::config::core::v3::Metadata metadata3;
  runFilter(metadata3, yaml_config3);

  const auto& filter_metadata3 = metadata3.filter_metadata();
  const auto it_namespace3 = filter_metadata3.find("test_namespace");
  ASSERT_NE(it_namespace3, filter_metadata3.end());
  checkKeyNumber(it_namespace3->second, "value", 0.0);
}

// Test Base64 decoding edge cases
TEST_F(SetMetadataFilterTest, Base64DecodingEdgeCases) {
  // Test with valid base64 but empty result after decode
  const std::string yaml_config1 = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: ""  # Empty string
        type: STRING
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata1;
  runFilter(metadata1, yaml_config1);

  // Empty format string should result in empty decoded value, which should be allowed
  const auto& filter_metadata1 = metadata1.filter_metadata();
  const auto it_namespace1 = filter_metadata1.find("test_namespace");
  ASSERT_NE(it_namespace1, filter_metadata1.end());
  checkKeyString(it_namespace1->second, "value", "");

  // Test with base64 padding
  const std::string yaml_config2 = R"EOF(
    metadata:
      - metadata_namespace: test_namespace
        format_string:
          text_format_source:
            inline_string: "dGVzdA=="  # "test" with padding
        type: STRING
        encode: BASE64
  )EOF";

  envoy::config::core::v3::Metadata metadata2;
  runFilter(metadata2, yaml_config2);

  const auto& filter_metadata2 = metadata2.filter_metadata();
  const auto it_namespace2 = filter_metadata2.find("test_namespace");
  ASSERT_NE(it_namespace2, filter_metadata2.end());
  checkKeyString(it_namespace2->second, "value", "test");
}

// Test complex configuration with mixed metadata types and operations
TEST_F(SetMetadataFilterTest, ComplexMixedConfiguration) {
  const std::string yaml_config = R"EOF(
    apply_on: BOTH
    metadata:
      - metadata_namespace: mixed_namespace
        value:
          existing_field: "existing_value"
      - metadata_namespace: mixed_namespace
        format_string:
          text_format_source:
            inline_string: "formatted_value"
        type: STRING
        allow_overwrite: true
      - metadata_namespace: no_overwrite_namespace
        value:
          field1: "value1"
      - metadata_namespace: no_overwrite_namespace
        format_string:
          text_format_source:
            inline_string: "should_not_overwrite"
        allow_overwrite: false
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runBiDirectionalFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();

  // Check mixed_namespace - should be merged
  const auto it_mixed = filter_metadata.find("mixed_namespace");
  ASSERT_NE(it_mixed, filter_metadata.end());
  checkKeyString(it_mixed->second, "existing_field", "existing_value");
  checkKeyString(it_mixed->second, "value", "formatted_value");

  // Check no_overwrite_namespace - should not be overwritten
  const auto it_no_overwrite = filter_metadata.find("no_overwrite_namespace");
  ASSERT_NE(it_no_overwrite, filter_metadata.end());
  checkKeyString(it_no_overwrite->second, "field1", "value1");

  // Should not have the "value" field since overwrite was denied
  const auto& fields = it_no_overwrite->second.fields();
  const auto value_it = fields.find("value");
  EXPECT_EQ(value_it, fields.end());

  // Check that overwrite_denied stat was incremented (should be called twice in bidirectional)
  EXPECT_EQ(4UL, config_->stats().overwrite_denied_.value());
}

// Test deprecated API mixed with new API
TEST_F(SetMetadataFilterTest, DeprecatedAndNewApiMixed) {
  const std::string yaml_config = R"EOF(
    metadata_namespace: deprecated_namespace
    value:
      deprecated_field: "deprecated_value"
    metadata:
      - metadata_namespace: new_namespace
        format_string:
          text_format_source:
            inline_string: "new_formatted_value"
        type: STRING
      - metadata_namespace: deprecated_namespace
        format_string:
          text_format_source:
            inline_string: "additional_formatted"
        allow_overwrite: true
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  runFilter(metadata, yaml_config);

  const auto& filter_metadata = metadata.filter_metadata();

  // Check deprecated namespace
  const auto it_deprecated = filter_metadata.find("deprecated_namespace");
  ASSERT_NE(it_deprecated, filter_metadata.end());
  checkKeyString(it_deprecated->second, "deprecated_field", "deprecated_value");
  checkKeyString(it_deprecated->second, "value", "additional_formatted");

  // Check new namespace
  const auto it_new = filter_metadata.find("new_namespace");
  ASSERT_NE(it_new, filter_metadata.end());
  checkKeyString(it_new->second, "value", "new_formatted_value");
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
