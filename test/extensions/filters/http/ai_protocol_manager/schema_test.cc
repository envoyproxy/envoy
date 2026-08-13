#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using StatusHelpers::IsOk;
using StatusHelpers::StatusCodeIs;

TEST(SchemaTest, PrimitiveTypesValidation) {
  EXPECT_THAT(Schema::string().validate(nlohmann::json("hello")), IsOk());
  EXPECT_THAT(Schema::string().validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(Schema::number().validate(nlohmann::json(3.14)), IsOk());
  EXPECT_THAT(Schema::number().validate(nlohmann::json(42)), IsOk());
  EXPECT_THAT(Schema::number().validate(nlohmann::json("not a number")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(Schema::integer().validate(nlohmann::json(42)), IsOk());
  EXPECT_THAT(Schema::integer().validate(nlohmann::json(3.14)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(Schema::boolean().validate(nlohmann::json(true)), IsOk());
  EXPECT_THAT(Schema::boolean().validate(nlohmann::json(false)), IsOk());
  EXPECT_THAT(Schema::boolean().validate(nlohmann::json("true")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(Schema::null().validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(Schema::null().validate(nlohmann::json("null")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(Schema::any().validate(nlohmann::json("anything")), IsOk());
  EXPECT_THAT(Schema::any().validate(nlohmann::json(123)), IsOk());
  EXPECT_THAT(Schema::any().validate(nlohmann::json(nlohmann::json::object())), IsOk());
}

TEST(SchemaTest, NumericRangeConstraints) {
  Schema temp_schema = Schema::number().range(0.0, 2.0);
  EXPECT_THAT(temp_schema.validate(nlohmann::json(0.0)), IsOk());
  EXPECT_THAT(temp_schema.validate(nlohmann::json(1.5)), IsOk());
  EXPECT_THAT(temp_schema.validate(nlohmann::json(2.0)), IsOk());
  EXPECT_THAT(temp_schema.validate(nlohmann::json(-0.1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(temp_schema.validate(nlohmann::json(2.1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Integer value checked against number range constraints.
  EXPECT_THAT(temp_schema.validate(nlohmann::json(1)), IsOk());
  EXPECT_THAT(temp_schema.validate(nlohmann::json(-1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(temp_schema.validate(nlohmann::json(3)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  Schema int_schema = Schema::integer().min(0).max(100);
  EXPECT_THAT(int_schema.validate(nlohmann::json(0)), IsOk());
  EXPECT_THAT(int_schema.validate(nlohmann::json(50)), IsOk());
  EXPECT_THAT(int_schema.validate(nlohmann::json(100)), IsOk());
  EXPECT_THAT(int_schema.validate(nlohmann::json(-1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(int_schema.validate(nlohmann::json(101)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, EnumStringValidation) {
  Schema role_schema =
      Schema::enumString({"system", "user", "assistant", "tool", "function", "developer"});

  EXPECT_THAT(role_schema.validate(nlohmann::json("user")), IsOk());
  EXPECT_THAT(role_schema.validate(nlohmann::json("assistant")), IsOk());
  EXPECT_THAT(role_schema.validate(nlohmann::json("invalid_role")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, OffloadableStringValidation) {
  Schema inline_only = Schema::string().offloadable(false);
  Schema offloadable = Schema::string().offloadable(true);

  nlohmann::json regular_str = "simple inline string";
  nlohmann::json offloaded_ref =
      JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 2048});

  EXPECT_THAT(inline_only.validate(regular_str), IsOk());
  EXPECT_THAT(offloadable.validate(regular_str), IsOk());

  EXPECT_THAT(offloadable.validate(offloaded_ref), IsOk());
  EXPECT_THAT(inline_only.validate(offloaded_ref),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, ObjectValidation) {
  Schema schema = Schema::object({
      {"model", Schema::string().required()},
      {"temperature", Schema::number()},
  });

  // Valid object with required field.
  nlohmann::json valid_obj = {{"model", "gpt-4"}};
  EXPECT_THAT(schema.validate(valid_obj), IsOk());

  // Valid object with both required and optional fields.
  nlohmann::json full_obj = {{"model", "gpt-4"}, {"temperature", 0.7}};
  EXPECT_THAT(schema.validate(full_obj), IsOk());

  // Missing required field.
  nlohmann::json missing_req = {{"temperature", 0.7}};
  auto status = schema.validate(missing_req);
  EXPECT_THAT(status, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(status.message(), "missing required field: model");

  // Invalid type for property.
  nlohmann::json bad_type = {{"model", 123}};
  EXPECT_THAT(schema.validate(bad_type), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Unknown fields allowed by default.
  nlohmann::json with_extra = {{"model", "gpt-4"}, {"extra_field", "permitted"}};
  EXPECT_THAT(schema.validate(with_extra), IsOk());

  // Strict unknown fields rejection.
  Schema strict_schema = Schema::object({
                                            {"model", Schema::string().required()},
                                        })
                             .allowUnknownFields(false);
  EXPECT_THAT(strict_schema.validate(valid_obj), IsOk());
  EXPECT_THAT(strict_schema.validate(with_extra), StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, ArrayValidation) {
  Schema schema = Schema::array(Schema::string()).min(1).max(2);

  nlohmann::json valid_arr = {"alpha", "beta"};
  EXPECT_THAT(schema.validate(valid_arr), IsOk());

  nlohmann::json empty_arr = nlohmann::json::array();
  EXPECT_THAT(schema.validate(empty_arr), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json too_large_arr = {"alpha", "beta", "gamma"};
  EXPECT_THAT(schema.validate(too_large_arr), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json bad_arr = {"alpha", 42};
  auto status = schema.validate(bad_arr);
  EXPECT_THAT(status, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(status.message(), "field '[1]' has invalid type: expected string, got integer");
}

TEST(SchemaTest, OneOfPolymorphicValidation) {
  Schema stop_schema = Schema::oneOf({
      Schema::string(),
      Schema::array(Schema::string()),
  });

  EXPECT_THAT(stop_schema.validate(nlohmann::json("STOP")), IsOk());
  EXPECT_THAT(stop_schema.validate(nlohmann::json({"STOP", "END"})), IsOk());
  EXPECT_THAT(stop_schema.validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(stop_schema.validate(nlohmann::json({123, 456})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, VectorOverloadsAndConstructors) {
  // Test std::vector<Property> overload for Schema::object.
  std::vector<Schema::Property> props;
  std::string prop_name = "key";
  props.emplace_back(prop_name, Schema::string());
  props.emplace_back("literal_key", Schema::integer());
  Schema obj_schema = Schema::object(std::move(props));
  EXPECT_EQ(obj_schema.type(), Schema::Type::Object);
  EXPECT_EQ(obj_schema.properties().size(), 2);

  // Test std::vector<Schema> overload for Schema::oneOf.
  std::vector<Schema> candidates;
  candidates.push_back(Schema::string());
  candidates.push_back(Schema::number());
  Schema one_of_schema = Schema::oneOf(std::move(candidates));
  EXPECT_EQ(one_of_schema.type(), Schema::Type::OneOf);
  EXPECT_EQ(one_of_schema.oneOfCandidates().size(), 2);
}

TEST(SchemaTest, CustomValidator) {
  Schema schema = Schema::string().customValidator([](const nlohmann::json& val) -> absl::Status {
    if (val.get<std::string>().rfind("prefix_", 0) == 0) {
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError("string must start with 'prefix_'");
  });

  EXPECT_THAT(schema.validate(nlohmann::json("prefix_foo")), IsOk());
  EXPECT_THAT(schema.validate(nlohmann::json("other_foo")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, OffloadableFieldPathsDiscovery) {
  Schema schema = Schema::object({
      {"model", Schema::string()},
      {"prompt", Schema::string().offloadable()},
      {"messages", Schema::array(Schema::object({
                       {"role", Schema::string()},
                       {"content", Schema::string().offloadable()},
                   }))},
  });

  const std::vector<std::string> expected = {"prompt", "messages[].content"};
  EXPECT_EQ(schema.offloadableFieldPaths(), expected);

  // Root offloadable string and array.
  Schema root_str = Schema::string().offloadable();
  EXPECT_EQ(root_str.offloadableFieldPaths(), std::vector<std::string>{""});

  Schema root_arr = Schema::array(Schema::string().offloadable());
  EXPECT_EQ(root_arr.offloadableFieldPaths(), std::vector<std::string>{"[]"});

  // OneOf candidate offloadable field path.
  Schema one_of = Schema::oneOf({
      Schema::string().offloadable(),
      Schema::array(Schema::string().offloadable()),
  });
  const std::vector<std::string> expected_one_of = {"", "[]"};
  EXPECT_EQ(one_of.offloadableFieldPaths(), expected_one_of);
}

TEST(SchemaTest, SchemaReflectionAndAccessors) {
  Schema enum_schema =
      Schema::enumString({"a", "b"}).required(true).offloadable(true).nullable(true);
  EXPECT_EQ(enum_schema.type(), Schema::Type::String);
  EXPECT_TRUE(enum_schema.isRequired());
  EXPECT_TRUE(enum_schema.isOffloadable());
  EXPECT_TRUE(enum_schema.isNullable());
  EXPECT_EQ(enum_schema.allowedValues(), (std::vector<std::string>{"a", "b"}));

  Schema non_nullable_schema = Schema::string();
  EXPECT_FALSE(non_nullable_schema.isNullable());
  non_nullable_schema.nullable(false);
  EXPECT_FALSE(non_nullable_schema.isNullable());

  Schema arr_schema = Schema::array(Schema::number());
  ASSERT_NE(arr_schema.elementSchema(), nullptr);
  EXPECT_EQ(arr_schema.elementSchema()->type(), Schema::Type::Number);

  Schema obj_schema = Schema::object({}).allowUnknownFields(false);
  EXPECT_FALSE(obj_schema.allowsUnknownFields());
}

TEST(SchemaTest, NullableValidation) {
  // Nullable string.
  Schema non_null_str = Schema::string();
  Schema null_str = Schema::string().nullable();
  EXPECT_THAT(non_null_str.validate(nlohmann::json("hello")), IsOk());
  EXPECT_THAT(non_null_str.validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(null_str.validate(nlohmann::json("hello")), IsOk());
  EXPECT_THAT(null_str.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_str.validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable number with range constraint.
  Schema null_num = Schema::number().range(0.0, 2.0).nullable();
  EXPECT_THAT(null_num.validate(nlohmann::json(1.0)), IsOk());
  EXPECT_THAT(null_num.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_num.validate(nlohmann::json(3.0)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(null_num.validate(nlohmann::json("text")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable integer with min/max constraint.
  Schema null_int = Schema::integer().min(0).max(10).nullable();
  EXPECT_THAT(null_int.validate(nlohmann::json(5)), IsOk());
  EXPECT_THAT(null_int.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_int.validate(nlohmann::json(-1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(null_int.validate(nlohmann::json(15)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable boolean.
  Schema null_bool = Schema::boolean().nullable();
  EXPECT_THAT(null_bool.validate(nlohmann::json(true)), IsOk());
  EXPECT_THAT(null_bool.validate(nlohmann::json(false)), IsOk());
  EXPECT_THAT(null_bool.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_bool.validate(nlohmann::json("true")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable object.
  Schema null_obj = Schema::object({{"key", Schema::string()}}).nullable();
  EXPECT_THAT(null_obj.validate(nlohmann::json{{"key", "val"}}), IsOk());
  EXPECT_THAT(null_obj.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_obj.validate(nlohmann::json("not_an_object")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable array and array with nullable elements.
  Schema null_arr = Schema::array(Schema::string()).nullable();
  EXPECT_THAT(null_arr.validate(nlohmann::json{"a", "b"}), IsOk());
  EXPECT_THAT(null_arr.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_arr.validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  Schema arr_with_null_elems = Schema::array(Schema::string().nullable());
  EXPECT_THAT(arr_with_null_elems.validate(nlohmann::json{"a", nullptr, "b"}), IsOk());
  EXPECT_THAT(arr_with_null_elems.validate(nlohmann::json{"a", 123}),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Nullable oneOf.
  Schema null_one_of = Schema::oneOf({Schema::string(), Schema::integer()}).nullable();
  EXPECT_THAT(null_one_of.validate(nlohmann::json("hello")), IsOk());
  EXPECT_THAT(null_one_of.validate(nlohmann::json(42)), IsOk());
  EXPECT_THAT(null_one_of.validate(nlohmann::json(nullptr)), IsOk());
  EXPECT_THAT(null_one_of.validate(nlohmann::json(true)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Required and nullable property in object.
  Schema req_null_obj = Schema::object({
      {"req_null", Schema::string().required().nullable()},
  });
  EXPECT_THAT(req_null_obj.validate(nlohmann::json{{"req_null", "val"}}), IsOk());
  EXPECT_THAT(req_null_obj.validate(nlohmann::json{{"req_null", nullptr}}), IsOk());
  EXPECT_THAT(req_null_obj.validate(nlohmann::json::object()),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(req_null_obj.validate(nlohmann::json{{"req_null", 123}}),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(SchemaTest, RequestResponseAndPayloadSchema) {
  // Default RequestSchema.
  RequestSchema default_req;
  EXPECT_TRUE(default_req.streamableFieldOrder().empty());
  EXPECT_TRUE(default_req.offloadableFieldPaths().empty());
  EXPECT_THAT(default_req.validate(nlohmann::json::object()), IsOk());

  // Default ResponseSchema.
  ResponseSchema default_resp;
  EXPECT_FALSE(default_resp.rootSchema().has_value());
  EXPECT_TRUE(default_resp.streamableFieldOrder().empty());
  EXPECT_TRUE(default_resp.offloadableFieldPaths().empty());

  // Parameterized ResponseSchema.
  ResponseSchema custom_resp(Schema::object({{"data", Schema::string().offloadable()}}), {"data"});
  EXPECT_TRUE(custom_resp.rootSchema().has_value());
  EXPECT_EQ(custom_resp.streamableFieldOrder(), std::vector<std::string>{"data"});
  EXPECT_EQ(custom_resp.offloadableFieldPaths(), std::vector<std::string>{"data"});

  // PayloadSchema.
  RequestSchema req(Schema::object({{"prompt", Schema::string().offloadable()}}), {"prompt"});
  PayloadSchema payload_schema(req, custom_resp);

  EXPECT_EQ(payload_schema.requestStreamableFieldOrder(), std::vector<std::string>{"prompt"});
  EXPECT_EQ(payload_schema.requestOffloadableFieldPaths(), std::vector<std::string>{"prompt"});
  EXPECT_EQ(payload_schema.requestSchema().streamableFieldOrder(),
            std::vector<std::string>{"prompt"});
  EXPECT_EQ(payload_schema.responseSchema().streamableFieldOrder(),
            std::vector<std::string>{"data"});

  // Validate request via JsonWithExtBuf and nlohmann::json.
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"prompt", "test"}});
  EXPECT_THAT(payload_schema.validateRequest(doc), IsOk());
  EXPECT_THAT(payload_schema.validateRequest(nlohmann::json{{"prompt", "test"}}), IsOk());
  EXPECT_THAT(payload_schema.requestSchema().validate(doc), IsOk());
  EXPECT_THAT(payload_schema.requestSchema().validate(nlohmann::json{{"prompt", "test"}}), IsOk());
  EXPECT_THAT(req.rootSchema().validate(doc), IsOk());
}

TEST(SchemaTest, TypeMismatchRejections) {
  // String schema given non-string.
  EXPECT_THAT(Schema::string().validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::string().validate(nlohmann::json::object()),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::string().validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::string().validate(nlohmann::json::binary({})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::string().validate(nlohmann::json(nlohmann::json::value_t::discarded)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Number schema given non-number.
  EXPECT_THAT(Schema::number().validate(nlohmann::json("abc")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::number().validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  nlohmann::json ext_ref = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 10});
  EXPECT_THAT(Schema::number().validate(ext_ref), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Integer schema given non-integer.
  EXPECT_THAT(Schema::integer().validate(nlohmann::json(true)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::integer().validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Boolean schema given non-boolean.
  EXPECT_THAT(Schema::boolean().validate(nlohmann::json(0)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::boolean().validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Null schema given non-null.
  EXPECT_THAT(Schema::null().validate(nlohmann::json(false)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::null().validate(nlohmann::json("text")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Object schema given non-object.
  EXPECT_THAT(Schema::object({}).validate(nlohmann::json("not_an_object")),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::object({}).validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Array schema given non-array.
  EXPECT_THAT(Schema::array(Schema::string()).validate(nlohmann::json(123)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Schema::array(Schema::string()).validate(nlohmann::json(nullptr)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
