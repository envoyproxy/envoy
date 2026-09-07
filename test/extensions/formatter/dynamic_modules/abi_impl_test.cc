#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/formatter/dynamic_modules/formatter.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {
namespace {

TEST(DynamicModuleFormatterAbiTest, HttpIntegerAttributesUseFormattingContext) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.protocol_ = Http::Protocol::Http11;
  stream_info.bytes_received_ = 11;
  stream_info.bytes_sent_ = 13;
  Http::TestRequestHeaderMapImpl request_headers{{"content-length", "17"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"grpc-status", "0"}};
  ::Envoy::Formatter::Context context(&request_headers, &response_headers, &response_trailers);
  FormatterContext formatter_context{&context, &stream_info};

  uint64_t result = 1;
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_RequestSize, &result));
  EXPECT_EQ(17, result);
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_RequestTotalSize, &result));
  EXPECT_EQ(11 + request_headers.byteSize(), result);
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_ResponseTotalSize, &result));
  EXPECT_EQ(13 + response_headers.byteSize() + response_trailers.byteSize(), result);
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_ResponseGrpcStatus, &result));
  EXPECT_EQ(0, result);
}

TEST(DynamicModuleFormatterAbiTest, HttpTotalSizeAttributesWithoutHeaderMaps) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.protocol_ = Http::Protocol::Http11;
  stream_info.bytes_received_ = 11;
  stream_info.bytes_sent_ = 13;
  ::Envoy::Formatter::Context context(nullptr, nullptr, nullptr);
  FormatterContext formatter_context{&context, &stream_info};

  uint64_t result = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_RequestTotalSize, &result));
  EXPECT_EQ(11, result);
  EXPECT_TRUE(envoy_dynamic_module_callback_formatter_get_attribute_int(
      &formatter_context, envoy_dynamic_module_type_attribute_id_ResponseTotalSize, &result));
  EXPECT_EQ(13, result);
}

TEST(DynamicModuleFormatterAbiTest, HttpIntegerAttributesRequireHttpProtocol) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl request_headers{{"content-length", "17"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  ::Envoy::Formatter::Context context(&request_headers, &response_headers, nullptr);
  FormatterContext formatter_context{&context, &stream_info};

  uint64_t result = 0;
  for (const auto id : {envoy_dynamic_module_type_attribute_id_RequestSize,
                        envoy_dynamic_module_type_attribute_id_RequestTotalSize,
                        envoy_dynamic_module_type_attribute_id_ResponseGrpcStatus,
                        envoy_dynamic_module_type_attribute_id_ResponseTotalSize}) {
    EXPECT_FALSE(
        envoy_dynamic_module_callback_formatter_get_attribute_int(&formatter_context, id, &result));
  }
}

} // namespace
} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
