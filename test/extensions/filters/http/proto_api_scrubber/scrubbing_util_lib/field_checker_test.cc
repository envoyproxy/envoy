#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

using proto_processing_lib::proto_scrubber::FieldCheckResults;
using proto_processing_lib::proto_scrubber::FieldFilters;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

inline constexpr const char kApiKeysDescriptorRelativePath[] = "test/proto/apikeys.descriptor";

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

// This tests CheckField() method for request fields.
TEST_F(FieldCheckerTest, RequestFieldChecker) {
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;

  FieldChecker field_checker(ScrubberContext::kRequestScrubbing, &mock_stream_info,
                             "/library.BookService/GetBook", filter_config_.get());

  {
    // The field `urn` doesn't have any match tree configured.
    Protobuf::Field field;
    field.set_name("urn");
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `shelf` has a match tree configured which always evaluates to false.
    // Hence, no match is found and CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("shelf");
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `id` has a match tree configured which always evaluates to true and has a match
    // action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("id");
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kExclude);
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
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `publisher` has a match tree configured which always evaluates to false.
    // Hence, no match is found and CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("publisher");
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kInclude);
  }

  {
    // The field `name` has a match tree configured which always evaluates to true and has a match
    // action configured of type
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`
    // and hence, CheckField returns kInclude.
    Protobuf::Field field;
    field.set_name("name");
    EXPECT_EQ(field_checker.CheckField({}, &field), FieldCheckResults::kExclude);
  }
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
