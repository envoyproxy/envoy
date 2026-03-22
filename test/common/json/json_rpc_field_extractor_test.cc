#include "source/common/json/json_rpc_field_extractor.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class ExtractorTestJsonRpcParserConfig : public JsonRpcParserConfig {
public:
  ExtractorTestJsonRpcParserConfig() { initializeDefaults(); }

protected:
  void initializeDefaults() override {
    always_extract_.insert("id");
    always_extract_.insert("jsonrpc");
    always_extract_.insert("method");
    addMethodConfig("method1", {AttributeExtractionRule("params.param1"),
                                AttributeExtractionRule("params.param2")});
    addMethodConfig("method2", {AttributeExtractionRule("params.nested.param3")});
    addMethodConfig(
        "method_types",
        {AttributeExtractionRule("params.bool"), AttributeExtractionRule("params.uint32"),
         AttributeExtractionRule("params.double"), AttributeExtractionRule("params.float"),
         AttributeExtractionRule("params.null"), AttributeExtractionRule("params.byte")});
  }
};

class TestJsonRpcFieldExtractor : public JsonRpcFieldExtractor {
public:
  TestJsonRpcFieldExtractor(Protobuf::Struct& metadata, const JsonRpcParserConfig& config)
      : JsonRpcFieldExtractor(metadata, config) {}

  bool list_supported = false;

protected:
  bool isNotification(const std::string& method) const override { return method == "notification"; }
  absl::string_view protocolName() const override { return "TestProtocol"; }
  absl::string_view jsonRpcVersion() const override { return "2.0"; }
  absl::string_view jsonRpcField() const override { return "jsonrpc"; }
  absl::string_view methodField() const override { return "method"; }
  bool lists_supported() const override { return list_supported; }
};

class JsonRpcFieldExtractorTest : public testing::Test {};

TEST_F(JsonRpcFieldExtractorTest, ExtractFields) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method1");
  extractor.StartObject("params");
  extractor.RenderString("param1", "value1");
  extractor.RenderInt32("param2", 123);
  extractor.EndObject();
  extractor.RenderInt32("id", 1);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method1", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method1", fields.at("method").string_value());
  EXPECT_EQ(1, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  EXPECT_EQ("value1", params.at("param1").string_value());
  EXPECT_EQ(123, params.at("param2").number_value());
}

TEST_F(JsonRpcFieldExtractorTest, NestedField) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method2");
  extractor.StartObject("params");
  extractor.StartObject("nested");
  extractor.RenderString("param3", "value3");
  extractor.EndObject();
  extractor.EndObject();
  extractor.RenderInt32("id", 2);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method2", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method2", fields.at("method").string_value());
  EXPECT_EQ(2, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  const auto& nested = params.at("nested").struct_value().fields();
  EXPECT_EQ("value3", nested.at("param3").string_value());
}

TEST_F(JsonRpcFieldExtractorTest, ListFieldSupported) {
  ExtractorTestJsonRpcParserConfig config;
  config.addMethodConfig("method_list", {AttributeExtractionRule("params.list")});
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);
  extractor.list_supported = true;

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method_list");
  extractor.StartObject("params");
  extractor.StartList("list");
  extractor.RenderString("", "value0");
  extractor.RenderString("", "value1");
  extractor.EndList();
  extractor.EndObject();
  extractor.RenderInt32("id", 3);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method_list", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method_list", fields.at("method").string_value());
  EXPECT_EQ(3, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  const auto& list = params.at("list").list_value();
  ASSERT_EQ(2, list.values_size());
  EXPECT_EQ("value0", list.values(0).string_value());
  EXPECT_EQ("value1", list.values(1).string_value());
}

TEST_F(JsonRpcFieldExtractorTest, NestedListField) {
  ExtractorTestJsonRpcParserConfig config;
  config.addMethodConfig("method_list", {AttributeExtractionRule("params.list")});
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);
  extractor.list_supported = true;

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method_list");
  extractor.StartObject("params");
  extractor.StartList("list");
  extractor.RenderString("", "value0");
  // nested list
  extractor.StartList("");
  extractor.RenderString("", "nested_value0");
  extractor.RenderString("", "nested_value1");
  extractor.EndList();
  extractor.RenderString("", "value1");
  extractor.EndList();
  extractor.EndObject();
  extractor.RenderInt32("id", 5);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method_list", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method_list", fields.at("method").string_value());
  EXPECT_EQ(5, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  const auto& list = params.at("list").list_value();
  ASSERT_EQ(3, list.values_size());
  EXPECT_EQ("value0", list.values(0).string_value());
  EXPECT_EQ("value1", list.values(2).string_value());

  const auto& nested_list = list.values(1).list_value();
  ASSERT_EQ(2, nested_list.values_size());
  EXPECT_EQ("nested_value0", nested_list.values(0).string_value());
  EXPECT_EQ("nested_value1", nested_list.values(1).string_value());
}

TEST_F(JsonRpcFieldExtractorTest, AllTypes) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method_types");
  extractor.StartObject("params");
  extractor.RenderBool("bool", true);
  extractor.RenderUint32("uint32", 4294967295);
  extractor.RenderDouble("double", 123.456);
  extractor.RenderFloat("float", 789.101f);
  extractor.RenderNull("null");
  extractor.RenderBytes("byte", "byte_value");
  extractor.EndObject();
  extractor.RenderInt32("id", 4);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method_types", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method_types", fields.at("method").string_value());
  EXPECT_EQ(4, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  EXPECT_TRUE(params.at("bool").bool_value());
  EXPECT_EQ(4294967295, params.at("uint32").number_value());
  EXPECT_EQ(123.456, params.at("double").number_value());
  EXPECT_NEAR(789.101, params.at("float").number_value(), 0.001);
  EXPECT_EQ(Protobuf::NULL_VALUE, params.at("null").null_value());
  EXPECT_EQ("byte_value", params.at("byte").string_value());
}

TEST_F(JsonRpcFieldExtractorTest, NoListSupport) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "method_list");
  extractor.StartObject("params");
  extractor.StartList("list");
  extractor.RenderString("", "value0");
  extractor.RenderBool("ignored_bool", true);
  extractor.RenderInt64("ignored_int64", 123);
  extractor.RenderUint64("ignored_uint64", 456);
  extractor.RenderDouble("ignored_double", 1.23);
  extractor.RenderNull("ignored_null");
  // This object should be ignored
  extractor.StartObject("");
  extractor.RenderString("a", "b");
  extractor.EndObject();
  extractor.RenderString("", "value1");
  extractor.EndList();
  extractor.EndObject();
  extractor.RenderInt32("id", 3);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("method_list", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(3, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("method_list", fields.at("method").string_value());
  EXPECT_EQ(3, fields.at("id").number_value());
  EXPECT_FALSE(fields.contains("params"));
}

TEST_F(JsonRpcFieldExtractorTest, EarlyStop) {
  ExtractorTestJsonRpcParserConfig config;
  config.addMethodConfig("early_stop_method", {AttributeExtractionRule("params.foo")});
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "early_stop_method");
  extractor.StartObject("params");
  extractor.RenderString("foo", "bar");
  extractor.EndObject();
  extractor.RenderInt32("id", 6);
  // can_stop_parsing_ should be true now.
  EXPECT_TRUE(extractor.shouldStopParsing());
  // This should be ignored.
  extractor.RenderString("ignored_param", "ignored_value");
  extractor.RenderBool("ignored_bool", true);
  extractor.RenderInt64("ignored_int64", 123);
  extractor.RenderUint64("ignored_uint64", 456);
  extractor.RenderDouble("ignored_double", 1.23);
  extractor.RenderNull("ignored_null");
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("early_stop_method", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(4, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("early_stop_method", fields.at("method").string_value());
  EXPECT_EQ(6, fields.at("id").number_value());
  const auto& params = fields.at("params").struct_value().fields();
  EXPECT_TRUE(params.contains("foo"));
  EXPECT_EQ("bar", params.at("foo").string_value());
  EXPECT_FALSE(fields.contains("ignored_param"));
}

TEST_F(JsonRpcFieldExtractorTest, EarlyStopNotification) {
  ExtractorTestJsonRpcParserConfig config;
  config.addMethodConfig("notification", {});
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "notification");
  // can_stop_parsing_ should be true now.
  EXPECT_TRUE(extractor.shouldStopParsing());
  // This should be ignored.
  extractor.RenderString("ignored_param", "ignored_value");
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_EQ("notification", extractor.getMethod());

  const auto& fields = metadata.fields();
  EXPECT_EQ(2, fields.size());
  EXPECT_EQ("2.0", fields.at("jsonrpc").string_value());
  EXPECT_EQ("notification", fields.at("method").string_value());
  EXPECT_FALSE(fields.contains("id"));
  EXPECT_FALSE(fields.contains("ignored_param"));
}

TEST_F(JsonRpcFieldExtractorTest, InvalidJsonRpcMissingVersion) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("method", "method1");
  extractor.RenderInt32("id", 1);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_FALSE(extractor.isValidJsonRpc());
}

TEST_F(JsonRpcFieldExtractorTest, InvalidJsonRpcMissingMethod) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderInt32("id", 1);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_FALSE(extractor.isValidJsonRpc());
}

} // namespace
} // namespace Json
} // namespace Envoy
