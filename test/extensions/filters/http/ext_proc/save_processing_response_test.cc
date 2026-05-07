#include "source/extensions/http/ext_proc/response_processors/save_processing_response/save_processing_response.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {
namespace {

using envoy::service::ext_proc::v3::ProcessingResponse;
using envoy::service::ext_proc::v3::StreamedImmediateResponse;

TEST(SaveProcessingResponseTest, SaveStreamingImmediateResponse) {
  envoy::extensions::http::ext_proc::response_processors::save_processing_response::v3::
      SaveProcessingResponse config;
  config.mutable_save_immediate_response()->set_save_response(true);

  SaveProcessingResponse processor(config);

  StreamedImmediateResponse response;
  response.mutable_headers_response()->set_end_of_stream(true);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  processor.afterProcessingStreamingImmediateResponse(response, absl::OkStatus(), stream_info);

  auto filter_state = stream_info.filterState()->getDataReadOnly<SaveProcessingResponseFilterState>(
      SaveProcessingResponseFilterState::kFilterStateName);
  ASSERT_NE(filter_state, nullptr);
  EXPECT_EQ(filter_state->response->processing_response.response_case(),
            ProcessingResponse::ResponseCase::kStreamedImmediateResponse);
  EXPECT_TRUE(TestUtility::protoEqual(
      filter_state->response->processing_response.streamed_immediate_response(), response));
}

TEST(SaveProcessingResponseTest, SaveStreamingImmediateResponseOnError) {
  envoy::extensions::http::ext_proc::response_processors::save_processing_response::v3::
      SaveProcessingResponse config;
  config.mutable_save_immediate_response()->set_save_response(true);
  config.mutable_save_immediate_response()->set_save_on_error(true);

  SaveProcessingResponse processor(config);

  StreamedImmediateResponse response;
  response.mutable_headers_response()->set_end_of_stream(true);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  auto error_status = absl::InternalError("test error");
  processor.afterProcessingStreamingImmediateResponse(response, error_status, stream_info);

  auto filter_state = stream_info.filterState()->getDataReadOnly<SaveProcessingResponseFilterState>(
      SaveProcessingResponseFilterState::kFilterStateName);
  ASSERT_NE(filter_state, nullptr);
  EXPECT_EQ(filter_state->response->processing_status, error_status);
}

TEST(SaveProcessingResponseTest, DontSaveStreamingImmediateResponseOnError) {
  envoy::extensions::http::ext_proc::response_processors::save_processing_response::v3::
      SaveProcessingResponse config;
  config.mutable_save_immediate_response()->set_save_response(true);
  config.mutable_save_immediate_response()->set_save_on_error(false);

  SaveProcessingResponse processor(config);

  StreamedImmediateResponse response;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  processor.afterProcessingStreamingImmediateResponse(response, absl::InternalError("test error"),
                                                      stream_info);

  auto filter_state = stream_info.filterState()->getDataReadOnly<SaveProcessingResponseFilterState>(
      SaveProcessingResponseFilterState::kFilterStateName);
  EXPECT_EQ(filter_state, nullptr);
}

} // namespace
} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
