#include "test/extensions/filters/http/ext_proc/filter_test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;

class HttpFilterExtraTest : public HttpFilterTest {
public:
  void initializeExtra(std::string yaml) { HttpFilterTest::initialize(std::move(yaml)); }
};

TEST_F(HttpFilterExtraTest, OnErrorFailureModeAllow) {
  initializeExtra(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: true
  )EOF");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Directly call onError.
  filter_->onError();

  EXPECT_EQ(1U, config_->stats().http_not_ok_resp_received_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
}

TEST_F(HttpFilterExtraTest, OnErrorFailureModeDeny) {
  initializeExtra(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  failure_mode_allow: false
  )EOF");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(encoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _, _));

  // Directly call onError.
  filter_->onError();

  EXPECT_EQ(1U, config_->stats().http_not_ok_resp_received_.value());
  EXPECT_EQ(0U, config_->stats().failure_mode_allowed_.value());
}

TEST_F(HttpFilterExtraTest, OnComplete) {
  initializeExtra(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  )EOF");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  envoy::service::ext_proc::v3::ProcessingResponse resp;
  resp.mutable_request_headers()->mutable_response()->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);

  EXPECT_CALL(decoder_callbacks_, continueDecoding());

  // Directly call onComplete.
  filter_->onComplete(resp);
}

TEST_F(HttpFilterExtraTest, OverrideMessageTimeout) {
  initializeExtra(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  message_timeout: 0.1s
  max_message_timeout: 1s
  )EOF");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  envoy::service::ext_proc::v3::ProcessingResponse resp;
  resp.mutable_override_message_timeout()->set_nanos(200000000); // 200ms

  filter_->onComplete(resp);

  EXPECT_EQ(1U, config_->stats().override_message_timeout_received_.value());

  // Disable timers to satisfy TearDown check.
  for (auto* timer : timers_) {
    timer->disableTimer();
  }
}

TEST_F(HttpFilterExtraTest, ObservabilityModeWrongBodyMode) {
  initializeExtra(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  observability_mode: true
  processing_mode:
    request_body_mode: BUFFERED
  )EOF");

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl data("foo");
  // Should hit the "Wrong body mode for observability mode" log.
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
