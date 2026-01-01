#include "envoy/common/optref.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
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
  MockProtoApiScrubberFilterConfig(Stats::Store& store, TimeSource& time_source)
      : ProtoApiScrubberFilterConfig(ProtoApiScrubberStats(*store.rootScope(), "mock_prefix."),
                                     time_source) {}

  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getRequestFieldMatcher,
              (const std::string& method_name, const std::string& field_mask), (const, override));

  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getResponseFieldMatcher,
              (const std::string& method_name, const std::string& field_mask), (const, override));

  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getMessageFieldMatcher,
              (const std::string& message_name, const std::string& field_name), (const, override));

  MOCK_METHOD(MatchTreeHttpMatchingDataSharedPtr, getMessageMatcher,
              (const std::string& message_name), (const, override));

  MOCK_METHOD(absl::StatusOr<absl::string_view>, getEnumName,
              (absl::string_view enum_type_name, int enum_value), (const, override));

  MOCK_METHOD(absl::StatusOr<const Protobuf::MethodDescriptor*>, getMethodDescriptor,
              (const std::string& method_name), (const, override));

  MOCK_METHOD(const Protobuf::Type*, getParentType, (const Protobuf::Field* field),
              (const, override));
};

namespace {

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";
inline constexpr const char kScrubberTestDescriptorRelativePath[] =
    "test/extensions/filters/http/proto_api_scrubber/scrubber_test.descriptor";

inline constexpr char kRemoveFieldActionType[] =
    "type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction";
inline constexpr char kRemoveFieldActionTypeWithoutPrefix[] =
    "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction";

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

// Helper to configure the Mock Config for Enum testing.
void setupMockEnumRule(MockProtoApiScrubberFilterConfig& mock_config, const std::string& method,
                       const std::string& field_path, const std::string& type_url, int enum_int,
                       absl::string_view enum_name, bool should_remove) {
  auto type_name = std::string(Envoy::TypeUtil::typeUrlToDescriptorFullName(type_url));

  ON_CALL(mock_config, getEnumName(type_name, enum_int)).WillByDefault(testing::Return(enum_name));

  // Mock Matcher Lookup
  std::string full_mask = absl::StrCat(field_path, ".", enum_name);
  auto match_tree = std::make_shared<NiceMock<MockMatchTree>>();

  if (should_remove) {
    auto remove_action = std::make_shared<NiceMock<MockAction>>();
    ON_CALL(*remove_action, typeUrl())
        .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
    ON_CALL(*match_tree, match(testing::_, testing::_))
        .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));
  } else {
    match_tree = nullptr; // No match
  }

  ON_CALL(mock_config, getRequestFieldMatcher(method, full_mask))
      .WillByDefault(testing::Return(match_tree));
  ON_CALL(mock_config, getResponseFieldMatcher(method, full_mask))
      .WillByDefault(testing::Return(match_tree));
}

// Custom Matcher to verify that HttpMatchingData contains specific Request Headers
MATCHER_P(HasRequestHeader, key, "") {
  const auto headers = arg.requestHeaders();
  if (!headers.has_value()) {
    return false;
  }
  return !headers->get(Http::LowerCaseString(key)).empty();
}

// Custom Matcher to verify that HttpMatchingData contains specific Response Headers
MATCHER_P(HasResponseHeader, key, "") {
  const auto headers = arg.responseHeaders();
  if (!headers.has_value()) {
    return false;
  }
  return !headers->get(Http::LowerCaseString(key)).empty();
}

// Custom Matcher to verify that HttpMatchingData contains specific Request Trailers
MATCHER_P(HasRequestTrailer, key, "") {
  const auto trailers = arg.requestTrailers();
  if (!trailers.has_value()) {
    return false;
  }
  return !trailers->get(Http::LowerCaseString(key)).empty();
}

// Custom Matcher to verify that HttpMatchingData contains specific Response Trailers
MATCHER_P(HasResponseTrailer, key, "") {
  const auto trailers = arg.responseTrailers();
  if (!trailers.has_value()) {
    return false;
  }
  return !trailers->get(Http::LowerCaseString(key)).empty();
}

class FieldCheckerTest : public ::testing::Test {
protected:
  FieldCheckerTest() : api_(Api::createApiForTest()) { setupMocks(); }

  enum class FieldType { Request, Response };

  void setupMocks() {
    // factory_context.serverFactoryContext().api() is used to read descriptor file during filter
    // config initialization. This mock setup ensures that test API is propagated properly to the
    // filter.
    ON_CALL(server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(server_factory_context_, timeSource()).WillByDefault(testing::ReturnRef(time_system_));
    ON_CALL(factory_context_, scope()).WillByDefault(testing::ReturnRef(*stats_store_.rootScope()));
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(testing::ReturnRef(server_factory_context_));
  }

  // Helper to load descriptors (shared by all configs).
  void loadDescriptors(ProtoApiScrubberConfig& config, const std::string& descriptor_path) {
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(descriptor_path))
            .value();
  }

  // Helper to initialize the filter config from a specific Proto config.
  void initializeFilterConfig(ProtoApiScrubberConfig& config,
                              const std::string& descriptor_path = kApiKeysDescriptorRelativePath) {
    loadDescriptors(config, descriptor_path);
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

    // CEL Matcher Template.
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
  Stats::IsolatedStoreImpl stats_store_;
  Event::SimulatedTimeSystem time_system_;
};

// This tests the scenarios where the underlying match tree returns incomplete matches for request
// and response field checkers.
TEST_F(FieldCheckerTest, IncompleteMatch) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string field_name = "user";

  Protobuf::Field field;
  field.set_name(field_name);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();

  // Return Not Found for method descriptor to bypass map logic (prevent segfault).
  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  EXPECT_CALL(*mock_match_tree, match(testing::_, testing::Eq(nullptr)))
      .WillRepeatedly(testing::Return(Matcher::MatchResult::insufficientData()));

  {
    EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker request_field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {},
                                       {}, {}, {}, method_name, &mock_filter_config);

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

    FieldChecker response_field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, {},
                                        {}, {}, {}, method_name, &mock_filter_config);

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

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  // Return Not Found for method descriptor.
  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  {
    // No match-action is configured.
    Matcher::MatchResult match_result(Matcher::ActionConstSharedPtr{nullptr});

    auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
    EXPECT_CALL(*mock_match_tree, match(testing::_, testing::Eq(nullptr)))
        .WillRepeatedly(testing::Return(match_result));

    EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(method_name, field_name))
        .WillOnce(testing::Return(mock_match_tree));

    FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {},
                               {}, method_name, &mock_filter_config);

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

    FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {},
                               {}, method_name, &mock_filter_config);

    // Assert that kInclude is returned because standard matching behavior dictates that
    // if an action is unknown to this specific filter, it should default to preserving the field.
    EXPECT_EQ(field_checker.CheckField({"user"}, &field), FieldCheckResults::kInclude);
  }
}

TEST_F(FieldCheckerTest, MessageLevelFieldRestriction) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string field_name = "secret_field";
  const std::string message_type = "example.v1.SensitiveMessage";

  Protobuf::Type parent_type;
  parent_type.set_name(message_type);

  Protobuf::Field field;
  field.set_name(field_name);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // When CheckField calls getParentType(&field), we MUST return &parent_type for this test logic
  // to succeed, mimicking the map lookup happening in the real config.
  EXPECT_CALL(mock_filter_config, getParentType(&field)).WillOnce(testing::Return(&parent_type));

  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*mock_match_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  EXPECT_CALL(mock_filter_config, getMessageFieldMatcher(message_type, field_name))
      .WillOnce(testing::Return(mock_match_tree));

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method_name, &mock_filter_config);

  // We intentionally pass nullptr as the parent_type to simulate the ProtoScrubber calling from
  // ScanField. The FieldChecker should recover the parent type using the mock_filter_config.
  FieldCheckResults result =
      field_checker.CheckField({"path", "to", "secret_field"}, &field, 0, nullptr);
  EXPECT_EQ(result, FieldCheckResults::kExclude);
}

TEST_F(FieldCheckerTest, GlobalMessageRestriction) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string message_type = "example.v1.RestrictedMessage";

  Protobuf::Field field;
  field.set_name("restricted_field");
  field.set_kind(Protobuf::Field::TYPE_MESSAGE);
  field.set_type_url("type.googleapis.com/" + message_type);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // Mock global message matcher to return Match + Remove Action.
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*mock_match_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  EXPECT_CALL(mock_filter_config, getMessageMatcher(message_type))
      .WillOnce(testing::Return(mock_match_tree));

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method_name, &mock_filter_config);

  // Should return kExclude based purely on the field type.
  EXPECT_EQ(field_checker.CheckField({"path", "to", "field"}, &field), FieldCheckResults::kExclude);
}

TEST_F(FieldCheckerTest, GlobalEnumRestriction) {
  const std::string method_name = "example.v1.Service/GetFoo";
  const std::string enum_type = "example.v1.RestrictedEnum";

  Protobuf::Field field;
  field.set_name("restricted_enum");
  field.set_kind(Protobuf::Field::TYPE_ENUM);
  field.set_type_url("type.googleapis.com/" + enum_type);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // Mock global enum matcher to return Match + Remove Action.
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*mock_match_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  EXPECT_CALL(mock_filter_config, getMessageMatcher(enum_type))
      .WillOnce(testing::Return(mock_match_tree));

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method_name, &mock_filter_config);

  // Should return kExclude based purely on the enum type.
  EXPECT_EQ(field_checker.CheckField({"path", "to", "enum"}, &field), FieldCheckResults::kExclude);
}

TEST_F(FieldCheckerTest, CheckType_GlobalRestriction) {
  const std::string message_type = "example.v1.RestrictedAnyPayload";
  Protobuf::Type type;
  type.set_name(message_type);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // Mock global message matcher to return Match + Remove Action.
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*mock_match_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  EXPECT_CALL(mock_filter_config, getMessageMatcher(message_type))
      .WillOnce(testing::Return(mock_match_tree));

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "dummy_method", &mock_filter_config);

  // CheckType is used when an Any field is unpacked. It should return kExclude.
  EXPECT_EQ(field_checker.CheckType(&type), FieldCheckResults::kExclude);
}

TEST_F(FieldCheckerTest, EnumTraversals) {
  const std::string enum_type = "example.v1.SafeEnum";
  Protobuf::Field field;
  field.set_name("safe_enum");
  field.set_kind(Protobuf::Field::TYPE_ENUM);
  field.set_type_url("type.googleapis.com/" + enum_type);

  NiceMock<MockProtoApiScrubberFilterConfig> mock_filter_config(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  ON_CALL(mock_filter_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // No global restrictions.
  EXPECT_CALL(mock_filter_config, getMessageMatcher(enum_type)).WillOnce(testing::Return(nullptr));

  // No path restrictions.
  EXPECT_CALL(mock_filter_config, getRequestFieldMatcher(testing::_, testing::_))
      .WillRepeatedly(testing::Return(nullptr));

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "dummy_method", &mock_filter_config);

  // Crucial Check: Must return kPartial for Enum to trigger value inspection.
  EXPECT_EQ(field_checker.CheckField({"safe_enum"}, &field), FieldCheckResults::kPartial);
}

using RequestFieldCheckerTest = FieldCheckerTest;

// Tests CheckField() method for primitive and message type request fields.
TEST_F(RequestFieldCheckerTest, PrimitiveAndMessageType) {
  ProtoApiScrubberConfig config;
  std::string method = "/apikeys.ApiKeys/CreateApiKey";

  addRestriction(config, method, "shelf", FieldType::Request, false);
  addRestriction(config, method, "filter_criteria.publication_details.original_release_info.year",
                 FieldType::Request, false);
  addRestriction(config, method, "id", FieldType::Request, true);
  addRestriction(config, method,
                 "filter_criteria.publication_details.original_release_info.region_code",
                 FieldType::Request, true);
  addRestriction(config, method, "key.key.display_name", FieldType::Request, true);

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

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
    // and hence, CheckField returns kExclude.
    Protobuf::Field field;
    field.set_name("id");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);
    EXPECT_EQ(field_checker.CheckField({"id"}, &field), FieldCheckResults::kExclude);
  }

  {
    // The field `key.key.display_name` has a match tree configured which always evaluates to true
    // and has a match action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kExclude.
    // While the field `key.key.internal_name` has a match tree configured which always evaluates
    // to false and hence, CheckField returns kInclude.
    Protobuf::Field field1;
    field1.set_name("display_name");
    field1.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"key", "key", "display_name"}, &field1),
              FieldCheckResults::kExclude);

    Protobuf::Field field2;
    field2.set_name("internal_name");
    field2.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"key", "key", "internal_name"}, &field2),
              FieldCheckResults::kInclude);
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
  std::string method = "/apikeys.ApiKeys/CreateApiKey";

  // Top-level repeated primitive: "tags" -> Remove.
  addRestriction(config, method, "tags", FieldType::Request, true);

  // Nested repeated primitive: "metadata.history.edits" -> Remove.
  addRestriction(config, method, "metadata.history.edits", FieldType::Request, true);

  // Repeated Message: "chapters" -> No Rule (Should result in Partial to scrub children).

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

  {
    // Case 1: Top-level repeated primitive (e.g., repeated string tags).
    // Configured to be removed.
    Protobuf::Field field;
    field.set_name("tags");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"tags"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 2: Deeply nested repeated primitive.
    // Path: metadata.history.edits.
    // Configured to be removed.
    Protobuf::Field field;
    field.set_name("edits");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"metadata", "history", "edits"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Case 3: Repeated Message (e.g., repeated Chapter chapters).
    // No specific matcher on the list itself.
    // Should return kPartial so the scrubber iterates over the elements.
    Protobuf::Field field;
    field.set_name("chapters");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"chapters"}, &field), FieldCheckResults::kPartial);
  }

  {
    // Case 4: Repeated Primitive with NO matcher.
    // Should return kInclude (keep the whole list).
    Protobuf::Field field;
    field.set_name("flags");
    field.set_kind(Protobuf::Field_Kind_TYPE_BOOL);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"flags"}, &field), FieldCheckResults::kInclude);
  }
}

// Tests CheckField() specifically for enum fields in the request.
TEST_F(RequestFieldCheckerTest, EnumType) {
  // Setup local mock config.
  auto mock_config =
      std::make_shared<NiceMock<MockProtoApiScrubberFilterConfig>>(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  const std::string method = "/pkg.Service/UpdateConfig";

  // Default: Return Not Found for method descriptor (Enum test doesn't need maps).
  ON_CALL(*mock_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // Field-Level Rule: Remove 'legacy_status' entirely.
  // Path passed to `CheckField()` method: "config.legacy_status" (No integer suffix yet).
  auto exclude_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*exclude_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  ON_CALL(*mock_config, getRequestFieldMatcher(method, "config.legacy_status"))
      .WillByDefault(testing::Return(exclude_tree));

  // Value-Level Rules: 'status' field.
  // Rule 1: "config.status.DEBUG_MODE" (99) -> Remove.
  setupMockEnumRule(*mock_config, method, "config.status", "type.googleapis.com/pkg.Status", 99,
                    "DEBUG_MODE", true);

  // Rule 2: "config.status.OK" (0) -> Keep.
  setupMockEnumRule(*mock_config, method, "config.status", "type.googleapis.com/pkg.Status", 0,
                    "OK", false);

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, mock_config.get());

  {
    // Scenario 1: Field-Level Scrubbing.
    // The scrubber checks the field definition before reading values.
    Protobuf::Field field;
    field.set_name("legacy_status");
    field.set_kind(Protobuf::Field::TYPE_ENUM);

    EXPECT_EQ(field_checker.CheckField({"config", "legacy_status"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Scenario 2: Value-Level Scrubbing (Specific Value matches Rule).
    // The scrubber reads value 99, translates to DEBUG_MODE.
    Protobuf::Field field;
    field.set_name("status");
    field.set_kind(Protobuf::Field::TYPE_ENUM);
    field.set_type_url("type.googleapis.com/pkg.Status");

    EXPECT_EQ(field_checker.CheckField({"config", "status", "99"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Scenario 3: Value-Level Pass-through (Specific Value has no Rule/Keep).
    // The scrubber reads value 0, translates to OK.
    Protobuf::Field field;
    field.set_name("status");
    field.set_kind(Protobuf::Field::TYPE_ENUM);
    field.set_type_url("type.googleapis.com/pkg.Status");

    // Returning kPartial for enum leaf nodes effectively acts as kInclude.
    EXPECT_EQ(field_checker.CheckField({"config", "status", "0"}, &field),
              FieldCheckResults::kPartial);
  }

  {
    // Scenario 4: Unknown Enum Value (Fallback).
    // Input: Value 123.
    // Logic: getEnumName returns error.
    // FieldChecker constructs mask "config.status.123".
    // No matcher for that mask -> kInclude.
    Protobuf::Field field;
    field.set_name("status");
    field.set_kind(Protobuf::Field::TYPE_ENUM);
    field.set_type_url("type.googleapis.com/pkg.Status");

    // Returning kPartial for enum leaf nodes effectively acts as kInclude.
    EXPECT_LOG_CONTAINS("warn", "Enum translation skipped", {
      EXPECT_EQ(field_checker.CheckField({"config", "status", "123"}, &field),
                FieldCheckResults::kPartial);
    });
  }
}

TEST_F(RequestFieldCheckerTest, MapType) {
  ProtoApiScrubberConfig config;
  // Use a service/method that actually HAS map fields (scrubber_test.proto).
  std::string method = "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub";

  // Configure rule to scrub the VALUES of the "tags" map.
  // The normalized path for "tags" map is "tags.value".
  addRestriction(config, method, "tags.value", FieldType::Request, true);

  initializeFilterConfig(config, kScrubberTestDescriptorRelativePath);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

  // Construct a fake map entry parent type to simulate traversal context.
  // Type name must match what's in scrubber_test.proto:
  // message ScrubRequest { map<string, string> tags = ... } -> ScrubRequest.TagsEntry
  Protobuf::Type map_entry_type;
  map_entry_type.set_name("test.extensions.filters.http.proto_api_scrubber.ScrubRequest.TagsEntry");
  auto* option = map_entry_type.add_options();
  option->set_name("map_entry");
  option->mutable_value()->PackFrom(Protobuf::BoolValue()); // Value not strictly checked by logic.

  // Test the CheckField call for a map value.
  // Path: ["tags", "some_random_key"]
  // Field: The 'value' field of the map entry (field #2).
  Protobuf::Field value_field;
  value_field.set_name("value");
  value_field.set_number(2);
  value_field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  // Perform the check.
  // The normalization logic should detect "tags" is a map, "some_random_key" is the key,
  // and normalize the path to "tags.value".
  FieldCheckResults result =
      field_checker.CheckField({"tags", "some_random_key"}, &value_field, 0, &map_entry_type);

  // Expect Exclusion because "tags.value" matches the configured rule.
  EXPECT_EQ(result, FieldCheckResults::kExclude);
}

using ResponseFieldCheckerTest = FieldCheckerTest;

// Tests CheckField() method for primitive and message type response fields.
TEST_F(ResponseFieldCheckerTest, PrimitiveAndMessageType) {
  ProtoApiScrubberConfig config;
  std::string method = "/apikeys.ApiKeys/CreateApiKey";

  addRestriction(config, method, "publisher", FieldType::Response, false);
  addRestriction(config, method, "fulfillment.primary_location.exact_coordinates.aisle",
                 FieldType::Response, false);
  addRestriction(config, method, "name", FieldType::Response, true);
  addRestriction(config, method, "fulfillment.primary_location.exact_coordinates.bin_number",
                 FieldType::Response, true);

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

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
    // and hence, CheckField returns kExclude.
    Protobuf::Field field;
    field.set_name("name");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    EXPECT_EQ(field_checker.CheckField({"name"}, &field), FieldCheckResults::kExclude);
  }

  {
    // The field `fulfillment.primary_location.exact_coordinates.bin_number` has a match tree
    // configured which always evaluates to true and has a match action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kExclude.
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
  std::string method = "/apikeys.ApiKeys/CreateApiKey";

  // Top-level repeated primitive: "comments" -> Remove.
  addRestriction(config, method, "comments", FieldType::Response, true);

  // Nested repeated primitive: "author.awards" -> Remove.
  addRestriction(config, method, "author.awards", FieldType::Response, true);

  // Repeated Message: "tags" -> No Rule (Should result in Partial to scrub children).

  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

  {
    // Case 1: Top-level repeated primitive.
    Protobuf::Field field;
    field.set_name("comments");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"comments"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 2: Nested repeated primitive.
    Protobuf::Field field;
    field.set_name("awards");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"author", "awards"}, &field), FieldCheckResults::kExclude);
  }

  {
    // Case 3: Repeated Message (e.g., repeated Book related_books).
    // No restriction on the list itself.
    // Should return kPartial to allow scrubbing inside individual books.
    Protobuf::Field field;
    field.set_name("related_books");
    field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"related_books"}, &field), FieldCheckResults::kPartial);
  }

  {
    // Case 4: Repeated Primitive with NO matcher.
    // Should return kInclude (keep the whole list).
    Protobuf::Field field;
    field.set_name("tags");
    field.set_kind(Protobuf::Field_Kind_TYPE_STRING);
    field.set_cardinality(Protobuf::Field_Cardinality_CARDINALITY_REPEATED);

    EXPECT_EQ(field_checker.CheckField({"tags"}, &field), FieldCheckResults::kInclude);
  }
}

// Tests CheckField() specifically for enum fields in the response.
TEST_F(ResponseFieldCheckerTest, EnumType) {
  auto mock_config =
      std::make_shared<NiceMock<MockProtoApiScrubberFilterConfig>>(stats_store_, time_system_);
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  const std::string method = "/pkg.Service/GetConfig";

  // Return Not Found for method descriptor.
  ON_CALL(*mock_config, getMethodDescriptor(testing::_))
      .WillByDefault(testing::Return(absl::NotFoundError("Method not found")));

  // Field-Level Rule: Remove 'internal_flags' entirely.
  auto exclude_tree = std::make_shared<NiceMock<MockMatchTree>>();
  auto remove_action = std::make_shared<NiceMock<MockAction>>();
  ON_CALL(*remove_action, typeUrl())
      .WillByDefault(testing::Return(kRemoveFieldActionTypeWithoutPrefix));
  ON_CALL(*exclude_tree, match(testing::_, testing::_))
      .WillByDefault(testing::Return(Matcher::MatchResult(remove_action)));

  ON_CALL(*mock_config, getResponseFieldMatcher(method, "config.internal_flags"))
      .WillByDefault(testing::Return(exclude_tree));

  // Value-Level Rules: 'state' field.
  // Rule: "config.state.DEPRECATED" (2) -> Remove.
  setupMockEnumRule(*mock_config, method, "config.state", "type.googleapis.com/pkg.State", 2,
                    "DEPRECATED", true);

  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, mock_config.get());

  {
    // Scenario 1: Field-Level Scrubbing.
    Protobuf::Field field;
    field.set_name("internal_flags");
    field.set_kind(Protobuf::Field::TYPE_ENUM);

    EXPECT_EQ(field_checker.CheckField({"config", "internal_flags"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Scenario 2: Value-Level Scrubbing (Specific Value matches Rule).
    Protobuf::Field field;
    field.set_name("state");
    field.set_kind(Protobuf::Field::TYPE_ENUM);
    field.set_type_url("type.googleapis.com/pkg.State");

    EXPECT_EQ(field_checker.CheckField({"config", "state", "2"}, &field),
              FieldCheckResults::kExclude);
  }

  {
    // Scenario 3: Value-Level Pass-through (No Rule for this value).
    // Value 1 ("ACTIVE") -> No rule -> Include.
    Protobuf::Field field;
    field.set_name("state");
    field.set_kind(Protobuf::Field::TYPE_ENUM);
    field.set_type_url("type.googleapis.com/pkg.State");

    // kPartial for enum leaf nodes acts as kInclude.
    EXPECT_LOG_CONTAINS("warn", "Enum translation skipped", {
      EXPECT_EQ(field_checker.CheckField({"config", "state", "1"}, &field),
                FieldCheckResults::kPartial);
    });
  }
}

TEST_F(ResponseFieldCheckerTest, MapType) {
  ProtoApiScrubberConfig config;
  // Use a service/method that actually HAS map fields (scrubber_test.proto).
  std::string method = "/test.extensions.filters.http.proto_api_scrubber.ScrubberTestService/Scrub";

  // Configure rule to scrub the VALUES of the "tags" map.
  // The normalized path for "tags" map is "tags.value".
  addRestriction(config, method, "tags.value", FieldType::Response, true);

  initializeFilterConfig(config, kScrubberTestDescriptorRelativePath);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info, {}, {}, {}, {},
                             method, filter_config_.get());

  // Construct a fake map entry parent type to simulate traversal context.
  // Map fields are repeated messages of a "MapEntry" type.
  // This type must have the "map_entry" option set to true.
  Protobuf::Type map_entry_type;
  map_entry_type.set_name("test.extensions.filters.http.proto_api_scrubber.ScrubRequest.TagsEntry");

  auto* option = map_entry_type.add_options();
  option->set_name("map_entry");
  Protobuf::BoolValue bool_val;
  bool_val.set_value(true);
  option->mutable_value()->PackFrom(bool_val);

  // Test the CheckField call
  // The proto_scrubber library passes:
  // - path: ["tags", "specific_key"]
  // - field: The 'value' field of the map entry (usually field #2)
  // - parent_type: The MapEntry type we constructed
  Protobuf::Field value_field;
  value_field.set_name("value");
  value_field.set_number(2);
  value_field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  // Perform the check
  FieldCheckResults result =
      field_checker.CheckField({"tags", "specific_key_123"}, &value_field, 0,
                               &map_entry_type // Passing the parent type triggers the map logic
      );

  // Expect Exclusion because "tags.value" matches the configured rule
  EXPECT_EQ(result, FieldCheckResults::kExclude);
}

TEST_F(FieldCheckerTest, UnsupportedScrubberContext) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Field field;
  field.set_name("user");
  field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  FieldChecker field_checker(ScrubberContext::kTestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  EXPECT_LOG_CONTAINS("warn", "Unsupported scrubber context enum value", {
    FieldCheckResults result = field_checker.CheckField({"user"}, &field);
    EXPECT_EQ(result, FieldCheckResults::kInclude);
  });
}

TEST_F(FieldCheckerTest, ConstructorPropagatesHeadersAndTrailersToMatchTree) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  NiceMock<MockProtoApiScrubberFilterConfig> mock_config(stats_store_, time_system_);
  std::string method = "method";
  std::string field_name = "target_field";
  Http::TestRequestHeaderMapImpl request_headers{{"x-req-header", "true"}};
  Http::TestResponseHeaderMapImpl response_headers{{"x-res-header", "true"}};
  Http::TestRequestTrailerMapImpl request_trailers{{"x-req-trailer", "true"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"x-res-trailer", "true"}};

  // Setup the MatchTree expectation
  // We want to prove that the 'matching_data' passed to the match() method
  // actually contains the data we passed to the FieldChecker constructor.
  auto mock_match_tree = std::make_shared<NiceMock<MockMatchTree>>();

  EXPECT_CALL(
      *mock_match_tree,
      match(testing::AllOf(HasRequestHeader("x-req-header"), HasResponseHeader("x-res-header"),
                           HasRequestTrailer("x-req-trailer"), HasResponseTrailer("x-res-trailer")),
            testing::_))
      .WillOnce(testing::Return(Matcher::MatchResult(Matcher::ActionConstSharedPtr{nullptr})));

  // Wire up the config to return our spying match tree.
  ON_CALL(mock_config, getRequestFieldMatcher(method, field_name))
      .WillByDefault(testing::Return(mock_match_tree));

  // Instantiate FieldChecker.
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, request_headers,
                             response_headers, request_trailers, response_trailers, method,
                             &mock_config);

  // Trigger the check. This calls tryMatch -> match_tree->match(matching_data_)
  Protobuf::Field field;
  field.set_name(field_name);
  field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  field_checker.CheckField({field_name}, &field);
}

TEST_F(FieldCheckerTest, IncludesType) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  Protobuf::Type type;
  type.set_name("type");
  // CheckType should now return kPartial to force unpacking of Any fields.
  EXPECT_EQ(field_checker.CheckType(&type), FieldCheckResults::kPartial);
}

TEST_F(FieldCheckerTest, SupportAny) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  // SupportAny should now return true.
  EXPECT_TRUE(field_checker.SupportAny());
}

TEST_F(FieldCheckerTest, FilterName) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  EXPECT_EQ(field_checker.FilterName(), FieldFilters::FieldMaskFilter);
}

// Tests that if no match tree is found for a field, and it is a message type, CheckField returns
// kPartial.
TEST_F(FieldCheckerTest, NoMatchFoundForMessageField) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  Protobuf::Field field;
  field.set_name("some_message_field");
  field.set_kind(Protobuf::Field_Kind_TYPE_MESSAGE);

  EXPECT_EQ(field_checker.CheckField({"some_message_field"}, &field), FieldCheckResults::kPartial);
}

// Tests that when `field` is nullptr (indicating an unknown field), CheckField returns kInclude.
TEST_F(FieldCheckerTest, UnknownFieldIsNull) {
  ProtoApiScrubberConfig config;
  initializeFilterConfig(config);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info, {}, {}, {}, {},
                             "/apikeys.ApiKeys/CreateApiKey", filter_config_.get());

  // Pass nullptr to simulate an unknown field.
  EXPECT_EQ(field_checker.CheckField({"some", "unknown", "field"}, nullptr),
            FieldCheckResults::kInclude);
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
