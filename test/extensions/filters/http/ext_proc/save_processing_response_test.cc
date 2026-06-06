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
