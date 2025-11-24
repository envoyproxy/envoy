#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

using proto_processing_lib::proto_scrubber::FieldCheckResults;
using proto_processing_lib::proto_scrubber::FieldFilters;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

// Mock class for `ProtoApiScrubberFilterConfig` class which allows that the match tree can be
// mocked so that matching scenarios can be tested.
class MockProtoApiScrubberFilterConfig : public ProtoApiScrubberFilterConfig {
public:
  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getRequestFieldMatcher,
              (const std::string& method_name, const std::string& field_mask), (const, override));

  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getResponseFieldMatcher,
              (const std::string& method_name, const std::string& field_mask), (const, override));
};

namespace {

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";
inline constexpr char kRemoveFieldActionType[] =
    "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction";

// Mock class for Matcher::Action to simulate actions other than RemoveFieldAction.
class MockAction : public Matcher::Action {
public:
  MOCK_METHOD(absl::string_view, typeUrl, (), (const, override));
};

// Mock class for `Matcher::MatchTree` to reproduce different responses from the `match()` method.
class MockMatchTree : public Matcher::MatchTree<HttpMatchingData> {
public:
  MOCK_METHOD(Matcher::MatchResult, match,
              (const HttpMatchingData& matching_data, Matcher::SkippedMatchCb skipped_match_cb),
              (override));
};

class FieldCheckerTest : public ::testing::Test {
protected:
  FieldCheckerTest() : api_(Api::createApiForTest()) { setupMocks(); }

  enum class FieldType { Request, Response };

  void setupMocks() {
    // factory_context.serverFactoryContext().api() is used to read descriptor file during filter
    // config initialization. This mock setup ensures that test API is propagated properly to the
    // filter.
    ON_CALL(server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(testing::ReturnRef(server_factory_context_));
  }

  // Helper to load descriptors (shared by all configs)
  void loadDescriptors(ProtoApiScrubberConfig& config) {
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
  }

  // Helper to initialize the filter config from a specific Proto config
  void initializeFilterConfig(ProtoApiScrubberConfig& config) {
    loadDescriptors(config);
    absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config =
        ProtoApiScrubberFilterConfig::create(config, factory_context_);
    ASSERT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    ASSERT_NE(filter_config.value(), nullptr);
    filter_config_ = std::move(filter_config.value());
  }

  /**
   * Utility to add a field restriction to a specific ProtoApiScrubberConfig object.
   */
  void addRestriction(ProtoApiScrubberConfig& config, const std::string& method_name,
                      const std::string& field_path, FieldType field_type, bool match_result) {

    // CEL Matcher Template
    static constexpr absl::string_view matcher_template = R"pb(
      matcher_list: {
        matchers: {
          predicate: {
            single_predicate: {
              input: {
                typed_config: {
                  [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] { }
                }
              }
              custom_match: {
                typed_config: {
                  [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                    expr_match: {
                      cel_expr_parsed: {
                        expr: {
                          id: 1
                          const_expr: {
                            bool_value: $0
                          }
                        }
                        source_info: {
                          syntax_version: "cel1"
                          location: "inline_expression"
                          positions: {
                            key: 1
                            value: 0
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          on_match: {
            action: {
              typed_config: {
                [$1] { }
              }
            }
          }
        }
      }
    )pb";

    std::string matcher_str =
        absl::Substitute(matcher_template, match_result ? "true" : "false", kRemoveFieldActionType);

    xds::type::matcher::v3::Matcher matcher;
    if (!Envoy::Protobuf::TextFormat::ParseFromString(matcher_str, &matcher)) {
      FAIL() << "Failed to parse generated matcher config.";
    }

    auto& method_restrictions = *config.mutable_restrictions()->mutable_method_restrictions();
    auto& method_config = method_restrictions[method_name];

    auto* field_map = (field_type == FieldType::Request)
                          ? method_config.mutable_request_field_restrictions()
                          : method_config.mutable_response_field_restrictions();

    *(*field_map)[field_path].mutable_matcher() = matcher;
  }

  Api::ApiPtr api_;
  std::shared_ptr<const ProtoApiScrubberFilterConfig> filter_config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
};

// This tests the scenarios where the underlying match tree returns incomplete matches for request
// and response field checkers.
TEST_F(FieldCheckerTest, IncompleteMatch) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string field_name = "user";

  Protobuf::Field field;
  field.set_name(field_name);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();

  EXPECT_CALL(*mock_match_tree, match(testing::_, testing::Eq(nullptr)))
      .WillRepeatedly(testing::Return(Matcher::MatchResult::insufficientData()));

  {
    EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker request_field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                                       method_name, &mock_filter_config);

    EXPECT_LOG_CONTAINS(
        "warn",
        "Error encountered while matching the field `user`. This field would be preserved. Error "
        "details: Matching couldn't complete due to insufficient data.",
        {
          FieldCheckResults result = request_field_checker.CheckField({"user"}, &field);
          EXPECT_EQ(result, FieldCheckResults::kInclude);
        });
  }

  {
    EXPECT_CALL(mock_filter_config, getResponseFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker response_field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info,
                                        method_name, &mock_filter_config);

    EXPECT_LOG_CONTAINS(
        "warn",
        "Error encountered while matching the field `user`. This field would be preserved. Error "
        "details: Matching couldn't complete due to insufficient data.",
        {
          FieldCheckResults result = response_field_checker.CheckField({"user"}, &field);
          EXPECT_EQ(result, FieldCheckResults::kInclude);
        });
  }
}

// Tests that the field should be preserved if the action configured in the respective matcher is
// unsupported by the ProtoApiScrubber filter. Ideally, this should not happen as it would fail
// during filter initialization itself. However, to future-proof the runtime code, this test case is
// added.
TEST_F(FieldCheckerTest, CompleteMatchWithUnsupportedAction) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string field_name = "user";

  Protobuf::Field field;
  field.set_name(field_name);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  {
    // No match-action is configured.
    Matcher::MatchResult match_result(Matcher::ActionConstSharedPtr{nullptr});

    auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
    EXPECT_CALL(*mock_match_tree, match(testing::_, testing::Eq(nullptr)))
        .WillRepeatedly(testing::Return(match_result));

    EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, method_name,
                               &mock_filter_config);

    // Assert that kInclude is returned because standard matching behavior dictates that
    // if an action is unknown to this specific filter, it should default to preserving the field.
    EXPECT_EQ(field_checker.CheckField({"user"}, &field), FieldCheckResults::kInclude);
  }

  {
    // A match-action different from `RemoveFieldAction` is configured.
    auto mock_action = std::make_shared<NiceMock<MockAction>>();
    ON_CALL(*mock_action, typeUrl())
        .WillByDefault(testing::Return("type.googleapis.com/google.protobuf.Empty"));

    Matcher::MatchResult match_result(mock_action);

    auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
    EXPECT_CALL(*mock_match_tree, match(testing::_, testing::Eq(nullptr)))
        .WillRepeatedly(testing::Return(match_result));

    EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, method_name,
                               &mock_filter_config);

    // Assert that kInclude is returned because standard matching behavior dictates that
    // if an action is unknown to this specific filter, it should default to preserving the field.
    EXPECT_EQ(field_checker.CheckField({"user"}, &field), FieldCheckResults::kInclude);
  }
}

using RequestFieldCheckerTest = FieldCheckerTest;

// Tests CheckField() method for primitive and message type request fields.
TEST_F(RequestFieldCheckerTest, PrimitiveAndMessageType) {
  ProtoApiScrubberConfig config;
  std::string method = "/library.BookService/GetBook";

  addRestriction(config, method, "shelf", FieldType::Request, false);
  addRestriction(config, method, "filter_criteria.publication_details.original_release_info.year",
                 FieldType::Request, false);
  addRestriction(config, method, "id", FieldType::Request, true);
  addRestriction(config, method,
                 "filter_criteria.publication_details.original_release_info.region_code",
                 FieldType::Request, true);

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, method,
                             filter_config_.get());

  {
    // The field `urn` doesn't have any match tree configured.
    Protobuf::Field field;
    field.set_name("urn");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"urn"}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `filter_criteria.publication_details.original_release_info.language` doesn't have
    // any match tree configured.
    Protobuf::Field field;
    field.set_name("language");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField(
                  {"filter_criteria", "publication_details", "original_release_info", "language"},
                  &field),
              FieldCheckResults::kInclude);
  }

  {
    // The field `shelf` has a match tree configured which always evaluates to false.
    // Hence, no match is found and CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("shelf");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);
    EXPECT_EQ(field_checker.CheckField({"shelf"}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `filter_criteria.publication_details.original_release_info.year` has a match tree
    // configured which always evaluates to false. Hence, no match is found and CheckField returns
    // kInclude.
    Protobuf::Field field;
    field.set_name("year");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);
    EXPECT_EQ(
        field_checker.CheckField(
            {"filter_criteria", "publication_details", "original_release_info", "year"}, &field),
        FieldCheckResults::kInclude);
  }

  {
    // The field `id` has a match tree configured which always evaluates to true and has a match
    // action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("id");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);
    EXPECT_EQ(field_checker.CheckField({"id"}, &field), FieldCheckResults::kExclude);
  }

  {
    // The field `filter_criteria.publication_details.original_release_info.region_code` has a match
    // tree configured which always evaluates to true and has a match action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("region_code");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);

    EXPECT_EQ(field_checker.CheckField({"filter_criteria", "publication_details",
                                        "original_release_info", "region_code"},
                                       &field),
              FieldCheckResults::kExclude);
  }

  {
    // The field `metadata` is of message type and doesn't have any match tree configured for it.
    // Hence, kPartial is expected.
    Protobuf::Field field;
    field.set_name("metadata");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    EXPECT_EQ(field_checker.CheckField({"metadata"}, &field), FieldCheckResults::kPartial);
  }

  {
    // The field `filter_criteria.publication_details.original_release_info` is of message type and
    // doesn't have any match tree configured for it. Hence, kPartial is expected.
    Protobuf::Field field;
    field.set_name("filter_criteria.publication_details.original_release_info");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    EXPECT_EQ(field_checker.CheckField(
                  {"filter_criteria", "publication_details", "original_release_info"}, &field),
              FieldCheckResults::kPartial);
  }
}

// Tests CheckField() specifically for repeated fields (Arrays) in the request.
TEST_F(RequestFieldCheckerTest, ArrayType) {
  ProtoApiScrubberConfig config;
  std::string method = "/library.BookService/UpdateBook";

  // Top-level repeated primitive: "tags" -> Remove
  addRestriction(config, method, "tags", FieldType::Request, true);

  // Nested repeated primitive: "metadata.history.edits" -> Remove
  addRestriction(config, method, "metadata.history.edits", FieldType::Request, true);

  // Repeated Message: "chapters" -> No Rule (Should result in Partial to scrub children)

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, method,
                             filter_config_.get());

  {
    // Case 1: Top-level repeated primitive (e.g., repeated string tags)
    // Configured to be removed.
    Protobuf::Field field;
    field.set_name("tags");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"tags"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 2: Deeply nested repeated primitive
    // Path: metadata.history.edits
    // Configured to be removed.
    Protobuf::Field field;
    field.set_name("edits");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"metadata", "history", "edits"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Case 3: Repeated Message (e.g., repeated Chapter chapters)
    // No specific matcher on the list itself.
    // Should return kPartial so the scrubber iterates over the elements.
    Protobuf::Field field;
    field.set_name("chapters");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"chapters"}, &field), FieldCheckResults::kPartial);
  }

  {
    // Case 4: Repeated Primitive with NO matcher
    // Should return kInclude (keep the whole list).
    Protobuf::Field field;
    field.set_name("flags");
    field.set_kind(Protobuf::Field_Kind_TYPE_BOOL);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"flags"}, &field), FieldCheckResults::kInclude);
  }
}

using ResponseFieldCheckerTest = FieldCheckerTest;

// Tests CheckField() method for primitive and message type response fields.
TEST_F(ResponseFieldCheckerTest, PrimitiveAndMessageType) {
  ProtoApiScrubberConfig config;
  std::string method = "/library.BookService/GetBook";

  addRestriction(config, method, "publisher", FieldType::Response, false);
  addRestriction(config, method, "fulfillment.primary_location.exact_coordinates.aisle",
                 FieldType::Response, false);
  addRestriction(config, method, "name", FieldType::Response, true);
  addRestriction(config, method, "fulfillment.primary_location.exact_coordinates.bin_number",
                 FieldType::Response, true);

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, method,
                             filter_config_.get());

  {
    // The field `author` doesn't have any match tree configured.
    Protobuf::Field field;
    field.set_name("author");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"author"}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `fulfillment.primary_location.exact_coordinates.shelf_level` doesn't have any match
    // tree configured.
    Protobuf::Field field;
    field.set_name("fulfillment.primary_location.exact_coordinates.shelf_level");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField(
                  {"fulfillment", "primary_location", "exact_coordinates", "shelf_level"}, &field),
              FieldCheckResults::kInclude);
  }

  {
    // The field `publisher` has a match tree configured which always evaluates to false.
    // Hence, no match is found and CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("publisher");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"publisher"}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `fulfillment.primary_location.exact_coordinates.aisle` has a match tree configured
    // which always evaluates to false. Hence, no match is found and CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("fulfillment.primary_location.exact_coordinates.aisle");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField(
                  {"fulfillment", "primary_location", "exact_coordinates", "aisle"}, &field),
              FieldCheckResults::kInclude);
  }

  {
    // The field `name` has a match tree configured which always evaluates to true and has a match
    // action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("name");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"name"}, &field), FieldCheckResults::kExclude);
  }

  {
    // The field `fulfillment.primary_location.exact_coordinates.bin_number` has a match tree
    // configured which always evaluates to true and has a match action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("fulfillment.primary_location.exact_coordinates.bin_number");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField(
                  {"fulfillment", "primary_location", "exact_coordinates", "bin_number"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // The field `metadata` is of message type and doesn't have any match tree configured for it.
    // Hence, kPartial is expected.
    Protobuf::Field field;
    field.set_name("metadata");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    EXPECT_EQ(field_checker.CheckField({"metadata"}, &field), FieldCheckResults::kPartial);
  }

  {
    // The field `fulfillment.primary_location.exact_coordinates` is of message type and doesn't
    // have any match tree configured for it. Hence, kPartial is expected.
    Protobuf::Field field;
    field.set_name("fulfillment.primary_location.exact_coordinates");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    EXPECT_EQ(
        field_checker.CheckField({"fulfillment", "primary_location", "exact_coordinates"}, &field),
        FieldCheckResults::kPartial);
  }
}

// Tests CheckField() specifically for repeated fields (Arrays) in the response.
TEST_F(ResponseFieldCheckerTest, ArrayType) {
  ProtoApiScrubberConfig config;
  std::string method = "/library.BookService/GetBook";

  // Top-level repeated primitive: "comments" -> Remove
  addRestriction(config, method, "comments", FieldType::Response, true);

  // Nested repeated primitive: "author.awards" -> Remove
  addRestriction(config, method, "author.awards", FieldType::Response, true);

  // Repeated Message: "tags" -> No Rule (Should result in Partial to scrub children)

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, method,
                             filter_config_.get());

  {
    // Case 1: Top-level repeated primitive
    Protobuf::Field field;
    field.set_name("comments");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"comments"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 2: Nested repeated primitive
    Protobuf::Field field;
    field.set_name("awards");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"author", "awards"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 3: Repeated Message (e.g., repeated Book related_books)
    // No restriction on the list itself.
    // Should return kPartial to allow scrubbing inside individual books.
    Protobuf::Field field;
    field.set_name("related_books");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"related_books"}, &field), FieldCheckResults::kPartial);
  }

  {
    // Case 4: Repeated Primitive with NO matcher
    // Should return kInclude (keep the whole list).
    Protobuf::Field field;
    field.set_name("tags");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"tags"}, &field), FieldCheckResults::kInclude);
  }
}

TEST_F(FieldCheckerTest, UnsupportedScrubberContext) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Field field;
  field.set_name("user");
  field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  FieldChecker field_checker(ScrubberContext::kTestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  EXPECT_LOG_CONTAINS("warn", "Unsupported scrubber context enum value", {
    FieldCheckResults result = field_checker.CheckField({"user"}, &field);
    EXPECT_EQ(result, FieldCheckResults::kInclude);
  });
}

TEST_F(FieldCheckerTest, IncludesType) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  Protobuf::Type type;
  type.set_name("type");
  EXPECT_EQ(field_checker.CheckType(&type), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, SupportAny) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  EXPECT_FALSE(field_checker.SupportAny());
}

TEST_F(FieldCheckerTest, FilterName) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  EXPECT_EQ(field_checker.FilterName(), FieldFilters::FieldMaskFilter);
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
