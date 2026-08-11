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
  Schema schema = Schema::array(Schema::string()).min(1);

  nlohmann::json valid_arr = {"alpha", "beta"};
  EXPECT_THAT(schema.validate(valid_arr), IsOk());

  nlohmann::json empty_arr = nlohmann::json::array();
  EXPECT_THAT(schema.validate(empty_arr), StatusCodeIs(absl::StatusCode::kInvalidArgument));

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
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
