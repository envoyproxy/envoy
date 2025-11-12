#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

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
  FieldCheckerTest() : api_(Api::createApiForTest()) {
    setupMocks();
    initDefaultProtoConfig();
    initFilterConfigFromDefaultProtoConfig();
  }

  void setupMocks() {
    // factory_context.serverFactoryContext().api() is used to read descriptor file during filter
    // config initialization. This mock setup ensures that test API is propagated properly to the
    // filter.
    ON_CALL(server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(testing::ReturnRef(server_factory_context_));
  }

  void initDefaultProtoConfig() {
    Protobuf::TextFormat::ParseFromString(getDefaultProtoConfig(), &proto_config_);
    *proto_config_.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
  }

  void initFilterConfigFromDefaultProtoConfig() {
    absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config =
        ProtoApiScrubberFilterConfig::create(proto_config_, factory_context_);
    ASSERT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    ASSERT_NE(filter_config.value(), nullptr);
    filter_config_ = std::move(filter_config.value());
  }

  std::string getDefaultProtoConfig() {
    return R"pb(
      descriptor_set: { }
      restrictions: {
        method_restrictions: {
          key: "/library.BookService/GetBook"
          value: {
            request_field_restrictions: {
              key: "shelf"
              value: {
                matcher: {
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
                                        bool_value: false
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            request_field_restrictions: {
              key: "filter_criteria.publication_details.original_release_info.year"
              value: {
                matcher: {
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
                                        bool_value: false
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            request_field_restrictions: {
              key: "id"
              value: {
                matcher: {
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
                                        bool_value: true
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            request_field_restrictions: {
              key: "filter_criteria.publication_details.original_release_info.region_code"
              value: {
                matcher: {
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
                                        bool_value: true
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            response_field_restrictions: {
              key: "publisher"
              value: {
                matcher: {
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
                                        bool_value: false
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            response_field_restrictions: {
              key: "fulfillment.primary_location.exact_coordinates.aisle"
              value: {
                matcher: {
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
                                        bool_value: false
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            response_field_restrictions: {
              key: "name"
              value: {
                matcher: {
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
                                        bool_value: true
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            response_field_restrictions: {
              key: "fulfillment.primary_location.exact_coordinates.bin_number"
              value: {
                matcher: {
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
                                        bool_value: true
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
                            [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] { }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    )pb";
  }

  Api::ApiPtr api_;
  ProtoApiScrubberConfig proto_config_;
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

// This tests CheckField() method for request fields.
TEST_F(FieldCheckerTest, RequestFieldChecker) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

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
    field.set_name("filter_criteria.publication_details.original_release_info.language");
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
    field.set_name("filter_criteria.publication_details.original_release_info.year");
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
    field.set_name("filter_criteria.publication_details.original_release_info.region_code");
    field.set_kind(Protobuf::Field_Kind_TYPE_INT64);
    EXPECT_EQ(field_checker.CheckField({"id"}, &field), FieldCheckResults::kExclude);
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

// This tests CheckField() method for response fields.
TEST_F(FieldCheckerTest, ResponseFieldChecker) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  FieldChecker field_checker(ScrubberContext::kResponseScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

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

TEST_F(FieldCheckerTest, UnsupportedScrubberContext) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Protobuf::Field field;
  field.set_name("user");
  field.set_kind(Protobuf::Field_Kind_TYPE_STRING);

  FieldChecker field_checker(ScrubberContext::kTestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  EXPECT_LOG_CONTAINS(
      "warn",
      "Error encountered while matching the field `user`. This field would be preserved. Internal "
      "error details: Unsupported scrubber context enum value: `0`. Supported values are: {1, 2}.",
      {
        FieldCheckResults result = field_checker.CheckField({"user"}, &field);
        EXPECT_EQ(result, FieldCheckResults::kInclude);
      });
}

TEST_F(FieldCheckerTest, IncludesType) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  Protobuf::Type type;
  type.set_name("type");
  EXPECT_EQ(field_checker.CheckType(&type), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, SupportAny) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  EXPECT_FALSE(field_checker.SupportAny());
}

TEST_F(FieldCheckerTest, FilterName) {
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
