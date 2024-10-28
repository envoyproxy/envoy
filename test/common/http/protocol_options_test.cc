#include "source/common/http/protocol_options.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(HttpUtility, ValidateStreamErrors) {
  // Both false, the result should be false.
  envoy::config::core::v3::Http2ProtocolOptions http2_options;
  EXPECT_FALSE(Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options)
                   .value()
                   .override_stream_error_on_invalid_http_message()
                   .value());

  // If the new value is not present, the legacy value is respected.
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_TRUE(Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // If the new value is present, it is used.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  http2_options.set_stream_error_on_invalid_http_messaging(false);
  EXPECT_TRUE(Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // Invert values - the new value should still be used.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(false);
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_FALSE(Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options)
                   .value()
                   .override_stream_error_on_invalid_http_message()
                   .value());
}

TEST(HttpUtility, ValidateStreamErrorsWithHcm) {
  envoy::config::core::v3::Http2ProtocolOptions http2_options;
  http2_options.set_stream_error_on_invalid_http_messaging(true);
  EXPECT_TRUE(Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options)
                  .value()
                  .override_stream_error_on_invalid_http_message()
                  .value());

  // If the HCM value is present it will take precedence over the old value.
  ProtobufWkt::BoolValue hcm_value;
  hcm_value.set_value(false);
  EXPECT_FALSE(
      Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options, true, hcm_value)
          .value()
          .override_stream_error_on_invalid_http_message()
          .value());
  // The HCM value will be ignored if initializeAndValidateOptions is told it is not present.
  EXPECT_TRUE(
      Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options, false, hcm_value)
          .value()
          .override_stream_error_on_invalid_http_message()
          .value());

  // The override_stream_error_on_invalid_http_message takes precedence over the
  // global one.
  http2_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  EXPECT_TRUE(
      Envoy::Http2::ProtocolOptions::initializeAndValidateOptions(http2_options, true, hcm_value)
          .value()
          .override_stream_error_on_invalid_http_message()
          .value());
}

} // namespace Envoy
