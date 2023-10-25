#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"

#include "test/mocks/http/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::Envoy::Http::TestRequestTrailerMapImpl;

class FilterConfigTestBase : public ::testing::Test {
protected:
  FilterConfigTestBase() : api_(Api::createApiForTest()) {}

  void parseConfigProto(absl::string_view str = R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
    request_field_extractions: {
      key: "key.name"
      value: {
      }
    }
  }
})pb") {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(str, &proto_config_));
  }

  Api::ApiPtr api_;
  GrpcFieldExtractionConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;
};

using FilterConfigTestOk = FilterConfigTestBase;
TEST_F(FilterConfigTestOk, DescriptorInline) {
  parseConfigProto();
  *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, DescriptorFromFile) {
  parseConfigProto();
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, AllSupportedTypes) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "supported_types.string"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.uint32"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.uint64"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.int32"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.int64"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.sint32"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.sint64"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.fixed32"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.fixed64"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.float"
      value: {
      }
    }
    request_field_extractions: {
      key: "supported_types.double"
      value: {
      }
    }
  }
})pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, RepeatedField) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "repeated_supported_types.string"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.uint32"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.uint64"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.int32"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.int64"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.sint32"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.sint64"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.fixed32"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.fixed64"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.float"
      value: {
      }
    }
    request_field_extractions: {
      key: "repeated_supported_types.double"
      value: {
      }
    }
  }
})pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

using FilterConfigTestException = FilterConfigTestBase;
TEST_F(FilterConfigTestException, ErrorParsingDescriptorInline) {
  parseConfigProto();
  *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() = "123";
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException, testing::HasSubstr("unable to parse proto descriptor from inline bytes:"));
}

TEST_F(FilterConfigTestException, ErrorParsingDescriptorFromFile) {
  parseConfigProto();
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem");
  EXPECT_THAT_THROWS_MESSAGE(std::make_unique<FilterConfig>(
                                 proto_config_, std::make_unique<ExtractorFactoryImpl>(), *api_),
                             EnvoyException,
                             testing::HasSubstr("unable to parse proto descriptor from file"));
}

TEST_F(FilterConfigTestException, UnsupportedDescriptorSourceTyep) {
  parseConfigProto();
  *proto_config_.mutable_descriptor_set()->mutable_inline_string() =
      api_->fileSystem()
          .fileReadToEnd(TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr("unsupported DataSource case `3` for configuring `descriptor_set`"));
}

TEST_F(FilterConfigTestException, GrpcMethodNotFoundInProtoDescriptor) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "not-found-in-proto-descriptor"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
  }
}
    )pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr("couldn't find the gRPC method `not-found-in-proto-descriptor` defined in "
                         "the proto descriptor"));
}

TEST_F(FilterConfigTestException, UndefinedPath) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "undefined-path"
      value: {
      }
    }
  }
}
    )pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: Invalid fieldPath (undefined-path): no 'undefined-path' field in 'type.googleapis.com/apikeys.CreateApiKeyRequest' message)"));
}

TEST_F(FilterConfigTestException, UnsupportedTypeBool) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "unsupported_types.bool"
      value: {
      }
    }
  }
}
    )pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: leaf node 'bool' must be numerical/string)"));
}

TEST_F(FilterConfigTestException, UnsupportedTypeMessage) {
  parseConfigProto(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "unsupported_types.message"
      value: {
      }
    }
  }
}
    )pb");
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: leaf node 'message' must be numerical/string)"));
}

} // namespace

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
