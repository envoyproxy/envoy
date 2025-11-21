#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
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
using Matcher::HasActionWithType;
using Matcher::HasNoMatch;
using StatusHelpers::HasStatus;
using StatusHelpers::IsOk;
using testing::AllOf;
using testing::HasSubstr;
using testing::NiceMock;

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";

// A class for testing filter config related capabilities eg, parsing and storing the filter
// config in internal data structures, etc.
class ProtoApiScrubberFilterConfigTest : public ::testing::Test {
protected:
  ProtoApiScrubberFilterConfigTest() : api_(Api::createApiForTest()) {
    setupMocks();
    initDefaultProtoConfig();
  }

  void initDefaultProtoConfig() {
    Protobuf::TextFormat::ParseFromString(getDefaultProtoConfig(), &proto_config_);
    *proto_config_.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
  }

  void setupMocks() {
    // factory_context.serverFactoryContext().api() is used to read descriptor file during filter
    // config initialization. This mock setup ensures that test API is propagated properly to the
    // filter.
    ON_CALL(server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(testing::ReturnRef(server_factory_context_));
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
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
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
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
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
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    return proto_config;
  }

  ProtoApiScrubberConfig getConfigWithMessageRestrictions() {
    std::string filter_conf_string = R"pb(
      descriptor_set: {}
      restrictions: {
        message_restrictions: {
          key: "package.MyMessage"
          value: {
            config: {
              matcher: {
                matcher_list: {
                  matchers: {
                    predicate: {
                      single_predicate: {
                        input: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3
                                 .HttpAttributesCelMatchInput] {}
                          }
                        }
                        custom_match: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                              expr_match: {
                                parsed_expr: { expr: { const_expr: { bool_value: true } } }
                              }
                            }
                          }
                        }
                      }
                    }
                    on_match: {
                      action: {
                        typed_config: {
                          [type.googleapis.com/envoy.extensions.filters.http
                               .proto_api_scrubber.v3.RemoveFieldAction] {}
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        message_restrictions: {
          key: "another.package.OtherMessage"
          value: {
            config: {
              matcher: {
                matcher_list: {
                  matchers: {
                    predicate: {
                      single_predicate: {
                        input: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3
                                 .HttpAttributesCelMatchInput] {}
                          }
                        }
                        custom_match: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                              expr_match: {
                                parsed_expr: { expr: { const_expr: { bool_value: false } } }
                              }
                            }
                          }
                        }
                      }
                    }
                    on_match: {
                      action: {
                        typed_config: {
                          [type.googleapis.com/envoy.extensions.filters.http
                               .proto_api_scrubber.v3.RemoveFieldAction] {}
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
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    return proto_config;
  }

  ProtoApiScrubberConfig getConfigWithMethodLevelRestriction() {
    std::string filter_conf_string = R"pb(
      descriptor_set : {}
      restrictions: {
        method_restrictions: {
          key: "/library.BookService/GetBook"
          value: {
            method_restriction: {
              matcher: {
                matcher_list: {
                  matchers: {
                    predicate: {
                      single_predicate: {
                        input: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.HttpAttributesCelMatchInput] {}
                          }
                        }
                        custom_match: {
                          typed_config: {
                            [type.googleapis.com/xds.type.matcher.v3.CelMatcher] {
                              expr_match: { parsed_expr: { expr: { const_expr: { bool_value: true } } } }
                            }
                          }
                        }
                      }
                    }
                    on_match: {
                      action: {
                        typed_config: {
                          [type.googleapis.com/envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction] {}
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
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    return proto_config;
  }

  ProtoApiScrubberConfig getConfigWithMessageName(absl::string_view message_name) {
    std::string filter_conf_string = absl::StrFormat(
        R"pb(
          descriptor_set : {}
          restrictions: {
            message_restrictions: {
              key: "%s"
              value: {}
            }
          }
        )pb",
        message_name);
    ProtoApiScrubberConfig proto_config;
    Protobuf::TextFormat::ParseFromString(filter_conf_string, &proto_config);
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    return proto_config;
  }

  Api::ApiPtr api_;
  ProtoApiScrubberConfig proto_config_;
  std::shared_ptr<const ProtoApiScrubberFilterConfig> filter_config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
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

TEST_F(ProtoApiScrubberFilterConfigTest, DescriptorValidations) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    // Top level `descriptor_set` not defined.
    ProtoApiScrubberConfig config;
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Unsupported DataSource case `0` for "
              "configuring `descriptor_set`");
  }

  {
    // Invalid descriptor format (non-binary) from inline bytes.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() = "123";
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr("Error encountered during config initialization. Unable to "
                                   "parse proto descriptor from inline bytes"));
  }

  {
    // Invalid descriptor format but invalid descriptor (eg, duplicate message definition) from
    // inline bytes.
    Protobuf::FileDescriptorProto file_proto;
    file_proto.set_name("test_file.proto");
    file_proto.set_package("test_package");
    file_proto.set_syntax("proto3");

    // Add duplicate message types to make the descriptor invalid.
    file_proto.add_message_type()->set_name("TestMessage");
    file_proto.add_message_type()->set_name("TestMessage");

    std::string invalid_binary_descriptor;
    file_proto.SerializeToString(&invalid_binary_descriptor);

    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        invalid_binary_descriptor;
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr("Error encountered during config initialization. Unable to "
                                   "parse proto descriptor from inline bytes"));
  }

  {
    // Invalid descriptor format but invalid descriptor (eg, duplicate message definition in two
    // separate files) from inline bytes.
    Envoy::Protobuf::FileDescriptorSet descriptor_set;

    Protobuf::FileDescriptorProto file1_proto;
    file1_proto.set_name("test_file1.proto");
    file1_proto.set_package("test_package");
    file1_proto.set_syntax("proto3");
    file1_proto.add_message_type()->set_name("TestMessage");

    Protobuf::FileDescriptorProto file2_proto;
    file2_proto.set_name("test_file2.proto");
    file2_proto.set_package("test_package");
    file2_proto.set_syntax("proto3");
    // Duplicate definition - TestMessage is already defined in test_file1.proto
    file2_proto.add_message_type()->set_name("TestMessage");

    descriptor_set.add_file()->CopyFrom(file1_proto);
    descriptor_set.add_file()->CopyFrom(file2_proto);

    std::string invalid_binary_descriptor;
    descriptor_set.SerializeToString(&invalid_binary_descriptor);

    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        invalid_binary_descriptor;
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(
        filter_config.status().message(),
        testing::HasSubstr("Error encountered during config initialization. Error occurred in file "
                           "`test_file2.proto` while trying to build proto descriptors."));
  }

  {
    // Valid descriptors from inline bytes.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_bytes() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    // Non-existent file path.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_filename() =
        TestEnvironment::runfilesPath("path/to/non-existent-file.descriptor");
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr(
                    "Error encountered during config initialization. Unable to read from file"));
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr("path/to/non-existent-file.descriptor"));
  }

  {
    // Invalid descriptors from file.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_filename() =
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem");
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr("Error encountered during config initialization. Unable to "
                                   "parse proto descriptor from file"));
    EXPECT_THAT(filter_config.status().message(),
                testing::HasSubstr("test/config/integration/certs/upstreamcacert.pem"));
  }

  {
    // Valid descriptors from file.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_filename() =
        TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath);
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    // Unsupported descriptor type - string.
    ProtoApiScrubberConfig config;
    *config.mutable_descriptor_set()->mutable_data_source()->mutable_inline_string() =
        api_->fileSystem()
            .fileReadToEnd(Envoy::TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath))
            .value();
    filter_config = ProtoApiScrubberFilterConfig::create(config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Unsupported DataSource case `3` for "
              "configuring `descriptor_set`");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, MethodNameValidations) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithMethodName(""), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(), "Error encountered during config initialization. "
                                                "Invalid method name: ''. Method name is empty.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/*"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService/*'. Method name contains '*' which is not supported.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("/library.*/*"),
                                                         factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        filter_config.status().message(),
        "Error encountered during config initialization. Invalid method name: '/library.*/*'. "
        "Method name contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("*"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(
        filter_config.status().message(),
        "Error encountered during config initialization. Invalid method name: '*'. Method name "
        "contains '*' which is not supported.");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService.GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService.GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("library.BookService/GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'library.BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("library.BookService.GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'library.BookService.GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library_BookService/GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library_BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library/BookService/GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library/BookService/GetBook'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: "
              "'/library.BookService/'. Method name should follow the gRPC format "
              "('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithMethodName("/./GetBook"),
                                                         factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid method name: '/./GetBook'. "
              "Method name should follow the gRPC format ('/package.ServiceName/MethodName').");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithMethodName("/library.BookService/GetBook"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, FieldMaskValidations) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask(""), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(), "Error encountered during config initialization. "
                                                "Invalid field mask: ''. Field mask is empty.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("*"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: '*'. Field mask "
              "contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book.*"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: 'book.*'. Field "
              "mask contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("*.book"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(filter_config.status().message(),
              "Error encountered during config initialization. Invalid field mask: '*.book'. Field "
              "mask contains '*' which is not supported.");
  }

  {
    filter_config =
        ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(getConfigWithFieldMask("book.inner_book"),
                                                         factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }

  {
    filter_config = ProtoApiScrubberFilterConfig::create(
        getConfigWithFieldMask("book.inner_book.debug_info"), factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, FilteringModeValidations) {
  ProtoApiScrubberConfig proto_config;
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(filtering_mode: 0)pb", &proto_config));
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_filename() =
        TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath);
    filter_config = ProtoApiScrubberFilterConfig::create(proto_config, factory_context_);
    EXPECT_EQ(filter_config.status().code(), absl::StatusCode::kOk);
    EXPECT_EQ(filter_config.status().message(), "");
    EXPECT_EQ(filter_config.value()->filteringMode(),
              FilteringMode::ProtoApiScrubberConfig_FilteringMode_OVERRIDE);
  }

  {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(filtering_mode: 999)pb", &proto_config));
    *proto_config.mutable_descriptor_set()->mutable_data_source()->mutable_filename() =
        TestEnvironment::runfilesPath(kApiKeysDescriptorRelativePath);
    filter_config = ProtoApiScrubberFilterConfig::create(proto_config, factory_context_);
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

TEST_F(ProtoApiScrubberFilterConfigTest, GetRequestType) {
  // 1. Initialize the config
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> config_or_status =
      ProtoApiScrubberFilterConfig::create(proto_config_, factory_context_);
  ASSERT_EQ(config_or_status.status().code(), absl::StatusCode::kOk);
  filter_config_ = std::move(config_or_status.value());

  {
    // Case 1: Valid Method Name
    // The method name passed from headers usually has the format /Package.Service/Method
    std::string method_name = "/apikeys.ApiKeys/CreateApiKey";

    absl::StatusOr<const Protobuf::Type*> type_or_status =
        filter_config_->getRequestType(method_name);

    ASSERT_EQ(type_or_status.status().code(), absl::StatusCode::kOk);
    ASSERT_NE(type_or_status.value(), nullptr);

    // Verify the resolved input type is correct
    EXPECT_EQ(type_or_status.value()->name(), "apikeys.CreateApiKeyRequest");
  }

  {
    // Case 2: Invalid Method Name (Not in descriptor)
    std::string method_name = "/apikeys.ApiKeys/NonExistentMethod";

    absl::StatusOr<const Protobuf::Type*> type_or_status =
        filter_config_->getRequestType(method_name);

    EXPECT_EQ(type_or_status.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(
        type_or_status.status().message(),
        testing::HasSubstr(
            "Unable to find method `apikeys.ApiKeys.NonExistentMethod` in the descriptor pool"));
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, GetTypeFinder) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> config_or_status =
      ProtoApiScrubberFilterConfig::create(proto_config_, factory_context_);
  ASSERT_EQ(config_or_status.status().code(), absl::StatusCode::kOk);
  filter_config_ = std::move(config_or_status.value());
  const auto& type_finder = filter_config_->getTypeFinder();

  {
    // Case 1: Resolve a known Type URL
    std::string valid_type_url = "type.googleapis.com/apikeys.CreateApiKeyRequest";
    const Protobuf::Type* type = type_finder(valid_type_url);

    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->name(), "apikeys.CreateApiKeyRequest");
  }

  {
    // Case 2: Resolve an unknown Type URL
    std::string invalid_type_url = "type.googleapis.com/apikeys.UnknownMessage";
    const Protobuf::Type* type = type_finder(invalid_type_url);

    EXPECT_EQ(type, nullptr);
  }
}

TEST_F(ProtoApiScrubberFilterConfigTest, ParseMessageRestrictions) {
  ProtoApiScrubberConfig proto_config = getConfigWithMessageRestrictions();
  auto filter_config_or_status =
      ProtoApiScrubberFilterConfig::create(proto_config, factory_context_);
  ASSERT_THAT(filter_config_or_status, IsOk());
  filter_config_ = filter_config_or_status.value();
  ASSERT_NE(filter_config_, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Http::Matching::HttpMatchingDataImpl http_matching_data_impl(mock_stream_info);

  auto matcher1 = filter_config_->getMessageMatcher("package.MyMessage");
  ASSERT_NE(matcher1, nullptr);
  EXPECT_THAT(
      matcher1->match(http_matching_data_impl),
      HasActionWithType("envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction"));

  auto matcher2 = filter_config_->getMessageMatcher("another.package.OtherMessage");
  ASSERT_NE(matcher2, nullptr);
  EXPECT_THAT(matcher2->match(http_matching_data_impl), HasNoMatch());

  auto matcher3 = filter_config_->getMessageMatcher("non.existent.Message");
  EXPECT_EQ(matcher3, nullptr);
}

TEST_F(ProtoApiScrubberFilterConfigTest, ParseMethodLevelRestriction) {
  ProtoApiScrubberConfig proto_config = getConfigWithMethodLevelRestriction();
  auto filter_config_or_status =
      ProtoApiScrubberFilterConfig::create(proto_config, factory_context_);
  ASSERT_THAT(filter_config_or_status, IsOk());
  filter_config_ = filter_config_or_status.value();
  ASSERT_NE(filter_config_, nullptr);

  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  Http::Matching::HttpMatchingDataImpl http_matching_data_impl(mock_stream_info);

  auto matcher1 = filter_config_->getMethodMatcher("/library.BookService/GetBook");
  ASSERT_NE(matcher1, nullptr);
  EXPECT_THAT(
      matcher1->match(http_matching_data_impl),
      HasActionWithType("envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction"));

  auto matcher2 = filter_config_->getMethodMatcher("/non.existent.Service/Method");
  EXPECT_EQ(matcher2, nullptr);
}

TEST_F(ProtoApiScrubberFilterConfigTest, MessageNameValidations) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config;

  EXPECT_THAT(ProtoApiScrubberFilterConfig::create(getConfigWithMessageName(""), factory_context_),
              HasStatus(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Invalid message name: ''. Message name is empty.")));

  EXPECT_THAT(
      ProtoApiScrubberFilterConfig::create(getConfigWithMessageName("NoPackageMessage"),
                                           factory_context_),
      HasStatus(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Invalid message name: 'NoPackageMessage'. Message name should be fully qualified")));
  EXPECT_THAT(
      ProtoApiScrubberFilterConfig::create(getConfigWithMessageName(".package.Message"),
                                           factory_context_),
      HasStatus(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Invalid message name: '.package.Message'. Message name should be fully qualified")));
  EXPECT_THAT(
      ProtoApiScrubberFilterConfig::create(getConfigWithMessageName("package.Message."),
                                           factory_context_),
      HasStatus(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Invalid message name: 'package.Message.'. Message name should be fully qualified")));
  EXPECT_THAT(
      ProtoApiScrubberFilterConfig::create(getConfigWithMessageName("package..Message"),
                                           factory_context_),
      HasStatus(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Invalid message name: 'package..Message'. Message name should be fully qualified")));
  EXPECT_THAT(ProtoApiScrubberFilterConfig::create(getConfigWithMessageName("package.Message"),
                                                   factory_context_),
              IsOk());
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
