#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/proto_api_scrubber/config.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::MethodRestrictions;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig;
using Http::HttpMatchingData;
using xds::type::matcher::v3::HttpAttributesCelMatchInput;
using MatchTreeHttpMatchingDataSharedPtr = Matcher::MatchTreeSharedPtr<HttpMatchingData>;
using testing::NiceMock;

// Base class for testing filter config related capabilities eg, parsing and storing the filter
// config in internal data structures, etc.
class ProtoApiScrubberFilterConfigTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterConfigTest() : api_(Api::createApiForTest()) {
    Protobuf::TextFormat::ParseFromString(getProtoConfig(), &proto_config_);
  }

  std::string getProtoConfig() {
    return R"pb(
      descriptor_set: { }
      restrictions: {
        method_restrictions: {
          key: "/library.BookService/GetBook"
          value: {
            response_field_restrictions: {
              key: "debug_info"
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
              key: "book.debug_info"
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
          }
        }
      }
    )pb";
  }

  Api::ApiPtr api_;
  ProtoApiScrubberConfig proto_config_;
  std::shared_ptr<ProtoApiScrubberFilterConfig> filter_config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

// Tests whether the match trees are initialized properly for each field mask.
TEST_F(ProtoApiScrubberFilterConfigTest, MatchTreeValidation) {
  absl::StatusOr<std::shared_ptr<ProtoApiScrubberFilterConfig>> filter_config =
      ProtoApiScrubberFilterConfig::create(proto_config_, factory_context_);
  ASSERT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
  ASSERT_NE(filter_config.value(), nullptr);
  filter_config_ = filter_config.value();

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Http::Matching::HttpMatchingDataImpl http_matching_data_impl(mock_stream_info);

  // Testing the match_tree for the field_mask `debug_info`.
  // The match expression in hardcoded to `true` for `debug_info` which should result in a match
  // and a corresponding action named `RemoveFieldAction`.
  MatchTreeHttpMatchingDataSharedPtr match_tree =
      filter_config_->getResponseFieldMatcher("/library.BookService/GetBook", "debug_info");
  ASSERT_NE(match_tree, nullptr);
  Matcher::MatchTree<HttpMatchingData>::MatchResult match_result =
      match_tree->match(http_matching_data_impl);
  ASSERT_EQ(match_result.match_state_, Matcher::MatchState::MatchComplete);
  ASSERT_TRUE(match_result.on_match_.has_value());
  EXPECT_EQ(match_result.on_match_.value().action_cb_()->typeUrl(),
            "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction");

  // Testing the match_tree for the field_mask `book.debug_info`.
  // The match expression in hardcoded to `false` for `book.debug_info` which should result in
  // no match.
  match_tree =
      filter_config_->getResponseFieldMatcher("/library.BookService/GetBook", "book.debug_info");
  ASSERT_NE(match_tree, nullptr);
  Matcher::MatchTree<HttpMatchingData>::MatchResult match_result2 =
      match_tree->match(http_matching_data_impl);
  ASSERT_EQ(match_result2.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_FALSE(match_result2.on_match_.has_value());

  // Test invalid method name.
  match_tree =
      filter_config_->getResponseFieldMatcher("/non.existent.service/method", "book.debug_info");
  ASSERT_EQ(match_tree, nullptr);

  // Test invalid field mask.
  match_tree = filter_config_->getResponseFieldMatcher("/library.BookService/GetBook",
                                                       "non.existent.field.mask");
  ASSERT_EQ(match_tree, nullptr);
}

struct MethodNameValidationTestCase {
  std::string method_name;
  absl::StatusCode expected_status_code;
  std::string expected_status_message;
};

class MethodNameValidation : public testing::TestWithParam<MethodNameValidationTestCase> {};

TEST_P(MethodNameValidation, ValidateSingleMethodConfig) {
  std::string filter_conf_string = absl::StrFormat(
      R"pb(
    restrictions: {
      method_restrictions: {
        key: "%s"
        value: { }
      }
    }
  )pb",
      GetParam().method_name);
  ProtoApiScrubberConfig proto_config;
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config));
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  absl::StatusOr<std::shared_ptr<ProtoApiScrubberFilterConfig>> filter_config =
      ProtoApiScrubberFilterConfig::create(proto_config, factory_context);
  EXPECT_EQ(filter_config.status().code(), GetParam().expected_status_code);
  EXPECT_EQ(filter_config.status().message(), GetParam().expected_status_message);
}

INSTANTIATE_TEST_SUITE_P(
    MethodNameValidationTestSuite, MethodNameValidation,
    testing::ValuesIn<MethodNameValidationTestCase>({
        {"", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: ''. Method name is "
         "empty."},
        {"/library.BookService/*", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'/library.BookService/*'. Method name contains '*' which is not supported."},
        {"/library.*/*", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: '/library.*/*'. "
         "Method name contains '*' which is not supported."},
        {"*", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: '*'. Method name "
         "contains '*' which is not supported."},
        {"/library.BookService.GetBook", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'/library.BookService.GetBook'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"library.BookService/GetBook", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'library.BookService/GetBook'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"library.BookService.GetBook", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'library.BookService.GetBook'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"/library_BookService/GetBook", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'/library_BookService/GetBook'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"/library/BookService/GetBook", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'/library/BookService/GetBook'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"/library.BookService/", absl::StatusCode::kInvalidArgument,
         "Error encountered during config initialization. Invalid method name: "
         "'/library.BookService/'. Method name should follow the gRPC format "
         "('/package.ServiceName/MethodName')."},
        {"/library.BookService/GetBook", absl::StatusCode::kOk, ""},
    }));

/*
 *UTs - Filter mode
 * method name validation
 * field_mask name validation
 * Factory creation failure - empty CEL expression (maybe a try catch is needed for empty cel
expression) Code to test envoy message exception as well

TEST(ProcessConfigurationTest, ThrowsEnvoyExceptionWithCorrectMessageForEmptyConfig) {
    std::string expected_message = "Configuration value cannot be empty.";
    try {
        processConfiguration("");
        FAIL() << "Expected Envoy::EnvoyException to be thrown."; // This line should not be reached
    } catch (const Envoy::EnvoyException& e) {
        // Compare the exception message with the expected message
        // e.what() returns a const char*
        EXPECT_STREQ(expected_message.c_str(), e.what());
        // If e.what() could be compared as std::string, you could use EXPECT_EQ:
        // EXPECT_EQ(expected_message, std::string(e.what()));
    } catch (...) {
        // Optional: Catch any other unexpected exception types
        FAIL() << "Expected Envoy::EnvoyException but caught a different type of exception.";
    }
}

TEST(ProcessConfigurationTest, ThrowsEnvoyExceptionWithCorrectMessageForInvalidData) {
    std::string expected_message = "Invalid data encountered in configuration.";
    try {
        processConfiguration("invalid_data");
        FAIL() << "Expected Envoy::EnvoyException to be thrown.";
    } catch (const Envoy::EnvoyException& e) {
        EXPECT_STREQ(expected_message.c_str(), e.what());
    }
    // No need to catch (...) if you are sure only EnvoyException can be thrown or
    // if other exceptions would correctly indicate a different test failure.
}

*/

/*
TEST_F(ProtoApiScrubberFilterConfigTest, FieldNameValidation) {

}

TEST_F(ProtoApiScrubberFilterConfigTest, RestrictionConfigValidation) {

}
*/

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
