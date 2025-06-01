#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/common/matcher/test_utility.h"
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
using ::Envoy::Matcher::HasActionWithType;
using ::Envoy::Matcher::HasNoMatch;
using testing::NiceMock;

// A class for testing filter config related capabilities eg, parsing and storing the filter
// config in internal data structures, etc.
class ProtoApiScrubberFilterConfigTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterConfigTest() : api_(Api::createApiForTest()) {
    Protobuf::TextFormat::ParseFromString(getDefaultProtoConfig(), &proto_config_);
  }

  std::string getDefaultProtoConfig() {
    return R"pb(
      descriptor_set: { }
      restrictions: {
        method_restrictions: {
          key: "/library.BookService/GetBook"
          value: {
            request_field_restrictions: {
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

  ProtoApiScrubberConfig getConfigWithMethodName(absl::string_view method_name) {
    std::string filter_conf_string = absl::StrFormat(
        R"pb(
    restrictions: {
      method_restrictions: {
        key: "%s"
        value: { }
      }
    }
  )pb",
        method_name);
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    return proto_config;
  }

  ProtoApiScrubberConfig getConfigWithFieldMask(absl::string_view field_mask) {
    std::string filter_conf_string = absl::StrFormat(
        R"pb(
        restrictions: {
          method_restrictions: {
            key: "/library.BookService/GetBook"
            value: {
              response_field_restrictions: {
                key: "%s"
                value: {}
              }
            }
          }
        }
  )pb",
        field_mask);
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    return proto_config;
  }

  ProtoApiScrubberConfig getConfigWithInputType(absl::string_view input_type) {
    std::string filter_conf_string = absl::StrFormat(R"pb(
      restrictions: {
        method_restrictions: {
          key: "/library.BookService/GetBook"
          value: {
            request_field_restrictions: {
              key: "debug_info"
              value: {
                matcher: {
                  matcher_list: {
                    matchers: {
                      predicate: {
                        single_predicate: {
                          input: {
                            typed_config: {
                              [%s] { }
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
    )pb",
                                                     input_type);
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    return proto_config;
  }

  Api::ApiPtr api_;
  ProtoApiScrubberConfig proto_config_;
  std::shared_ptr<const ProtoApiScrubberFilterConfig> filter_config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
};

// Tests whether the match trees are initialized properly for each field mask.
TEST_F(ProtoApiScrubberFilterConfigTest, MatchTreeValidation) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Http::Matching::HttpMatchingDataImpl http_matching_data_impl(mock_stream_info);
  MatchTreeHttpMatchingDataSharedPtr match_tree;

  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config =
      ProtoApiScrubberFilterConfig::create(proto_config_, factory_context_);
  ASSERT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
  ASSERT_NE(filter_config.value(), nullptr);
  filter_config_ = std::move(filter_config.value());

  {
    // Testing the match_tree for the field_mask `debug_info`.
    // The match expression in hardcoded to `true` for `debug_info` which should result in a match
    // and a corresponding action named `RemoveFieldAction`.
    match_tree =
        filter_config_->getRequestFieldMatcher("/library.BookService/GetBook", "debug_info");
    ASSERT_NE(match_tree, nullptr);
    EXPECT_THAT(
        match_tree->match(http_matching_data_impl),
        HasActionWithType("envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction"));
  }

  {
    // Testing the match_tree for the field_mask `book.debug_info`.
    // The match expression in hardcoded to `false` for `book.debug_info` which should result in
    // no match.
    match_tree =
        filter_config_->getResponseFieldMatcher("/library.BookService/GetBook", "book.debug_info");
    ASSERT_NE(match_tree, nullptr);
    EXPECT_THAT(match_tree->match(http_matching_data_impl), HasNoMatch());
  }

  {
    // Validate invalid method name for request field matchers.
    match_tree = filter_config_->getRequestFieldMatcher("/non.existent.service/method", "book");
    ASSERT_EQ(match_tree, nullptr);
  }

  {
    // Validate invalid method name for response field matchers.
    match_tree =
        filter_config_->getResponseFieldMatcher("/non.existent.service/method", "book.debug_info");
    ASSERT_EQ(match_tree, nullptr);
  }

  {
    // Validate invalid field mask for request field matchers.
    match_tree = filter_config_->getRequestFieldMatcher("/library.BookService/GetBook",
                                                        "non.existent.field.mask");
    ASSERT_EQ(match_tree, nullptr);
  }

  {
    // Validate invalid field mask for response field matchers.
    match_tree = filter_config_->getResponseFieldMatcher("/library.BookService/GetBook",
                                                         "non.existent.field.mask");
    ASSERT_EQ(match_tree, nullptr);
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, MethodNameValidations) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithMethodName(""), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(), "Error encountered during config initialization. "
                                                "Invalid method name: ''. Method name is empty.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/*"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService/*'. Method name contains '*' which is not supported.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("/library.*/*"),
                                                         factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        filter_config.status().message(),
        "Error encountered during config initialization. Invalid method name: '/library.*/*'. "
        "Method name contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("*"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        filter_config.status().message(),
        "Error encountered during config initialization. Invalid method name: '*'. Method name "
        "contains '*' which is not supported.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService.GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService.GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("library.BookService/GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'library.BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("library.BookService.GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'library.BookService.GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library_BookService/GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library_BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library/BookService/GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library/BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService/'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("/./GetBook"),
                                                         factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: '/./GetBook'. "
              "Method name should follow the gRPC format ('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/GetBook"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, FieldMaskValidations) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask(""), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(), "Error encountered during config initialization. "
                                                "Invalid field mask: ''. Field mask is empty.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("*"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: '*'. Field mask "
              "contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book.*"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: 'book.*'. Field "
              "mask contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("*.book"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: '*.book'. Field "
              "mask contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book.inner_book"),
                                                         factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithFieldMask("book.inner_book.debug_info"), factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, FilteringModeValidations) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ProtoApiScrubberConfig proto_config;
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(filtering_mode: 0)pb", &proto_config));
    filter_config = ProtoApiScrubberFilterConfig::create(proto_config, factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
    EXPECT_EQ(filter_config.value()->filteringMode(),
              FilteringMode::ProtoApiScrubberConfig_FilteringMode_OVERRIDE);
  }

  {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(filtering_mode: 999)pb", &proto_config));
    filter_config = ProtoApiScrubberFilterConfig::create(proto_config, factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Unsupported 'filtering_mode': .");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, MatcherInputTypeValidations) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    EXPECT_THROW_WITH_MESSAGE(
        filter_config = ProtoApiScrubberFilterConfig::create(
            getConfigWithInputType(
                "type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput"),
            factory_context),
        EnvoyException,
        "Unsupported data input type: string. The matcher supports input type: cel_data_input");
  }

  {
    EXPECT_THROW_WITH_MESSAGE(
        filter_config = ProtoApiScrubberFilterConfig::create(
            getConfigWithInputType(
                "type.googleapis.com/"
                "envoy.extensions.matching.common_inputs.network.v3.ServerNameInput"),
            factory_context),
        EnvoyException,
        "Unsupported data input type: string. The matcher supports input type: cel_data_input");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithInputType(
            "type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput"),
        factory_context);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
