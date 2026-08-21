#include "source/common/json/json_rpc_field_extractor.h"

#include "test/test_common/struct_matchers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoubleNear;
using testing::ElementsAre;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Json {
namespace {

class ExtractorTestJsonRpcParserConfig : public JsonRpcParserConfig {
public:
  ExtractorTestJsonRpcParserConfig() { initializeDefaultsImpl(); }

protected:
  void initializeDefaults() override { initializeDefaultsImpl(); }
  void initializeDefaultsImpl() {
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

  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("method", "method1"),
                  IsStructNumber("id", 1),
                  IsStructStruct("params", UnorderedElementsAre(IsStructString("param1", "value1"),
                                                                IsStructNumber("param2", 123)))));
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

  EXPECT_THAT(
      metadata.fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructString("method", "method2"),
          IsStructNumber("id", 2),
          IsStructStruct(
              "params", UnorderedElementsAre(IsStructStruct(
                            "nested", UnorderedElementsAre(IsStructString("param3", "value3")))))));
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

  EXPECT_THAT(
      metadata.fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructString("method", "method_list"),
          IsStructNumber("id", 3),
          IsStructStruct("params", UnorderedElementsAre(IsStructList(
                                       "list", ElementsAre(IsStructValueString("value0"),
                                                           IsStructValueString("value1")))))));
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

  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("method", "method_list"),
                  IsStructNumber("id", 5),
                  IsStructStruct("params",
                                 UnorderedElementsAre(IsStructList(
                                     "list", ElementsAre(IsStructValueString("value0"),
                                                         IsStructValueList(ElementsAre(
                                                             IsStructValueString("nested_value0"),
                                                             IsStructValueString("nested_value1"))),
                                                         IsStructValueString("value1")))))));
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

  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("method", "method_types"),
                  IsStructNumber("id", 4),
                  IsStructStruct("params", UnorderedElementsAre(
                                               IsStructBool("bool", true),
                                               IsStructNumber("uint32", 4294967295),
                                               IsStructNumber("double", 123.456),
                                               IsStructNumber("float", DoubleNear(789.101, 0.001)),
                                               IsStructNull("null", Protobuf::NULL_VALUE),
                                               IsStructString("byte", "byte_value")))));
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

  EXPECT_THAT(metadata.fields(), UnorderedElementsAre(IsStructString("jsonrpc", "2.0"),
                                                      IsStructString("method", "method_list"),
                                                      IsStructNumber("id", 3)));
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

  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("method", "early_stop_method"),
                  IsStructNumber("id", 6),
                  IsStructStruct("params", UnorderedElementsAre(IsStructString("foo", "bar")))));
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

  EXPECT_THAT(metadata.fields(), UnorderedElementsAre(IsStructString("jsonrpc", "2.0"),
                                                      IsStructString("method", "notification")));
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

TEST_F(JsonRpcFieldExtractorTest, ResponseWithResult) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("result", "success");
  extractor.RenderInt32("id", 1);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(IsStructString("result", "success"), IsStructNumber("id", 1),
                                   IsStructString("jsonrpc", "2.0")));
}

TEST_F(JsonRpcFieldExtractorTest, ResponseWithError) {
  ExtractorTestJsonRpcParserConfig config;
  Protobuf::Struct metadata;
  TestJsonRpcFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.StartObject("error");
  extractor.RenderInt32("code", -32602);
  extractor.RenderString("message", "Invalid parameters");
  extractor.EndObject();
  extractor.RenderInt32("id", 1);
  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidJsonRpc());
  EXPECT_THAT(metadata.fields(),
              UnorderedElementsAre(
                  IsStructStruct("error", UnorderedElementsAre(
                                              IsStructNumber("code", -32602),
                                              IsStructString("message", "Invalid parameters"))),
                  IsStructNumber("id", 1), IsStructString("jsonrpc", "2.0")));
}

TEST_F(JsonRpcFieldExtractorTest, InvalidJsonRpcResponseMissingResultAndError) {
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
