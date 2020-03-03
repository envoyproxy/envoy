#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class MockThreadLocalController : public ThreadLocal::ThreadLocalObject, public ThreadLocalController {
public:
  MockThreadLocalController() {}
  MOCK_METHOD(uint32_t, requestTotalCount, ());
  MOCK_METHOD(uint32_t, requestSuccessCount, ());
  MOCK_METHOD(void, recordSuccess, ());
  MOCK_METHOD(void, recordFailure, ());
};

class AdmissionControlTest : public testing::Test {
public:
  AdmissionControlTest() {}

  std::shared_ptr<AdmissionControlFilterConfig> makeConfig(const std::string& yaml) {
    AdmissionControlFilterConfig::AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto tls = context_.threadLocal().allocateSlot();
    tls->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return controller_;
    });
    return std::make_shared<AdmissionControlFilterConfig>(proto, runtime_, time_system_, random_,
                                                          scope_, std::move(tls));
  }

  void setupFilter(std::shared_ptr<AdmissionControlFilterConfig> config) {
    filter_ = std::make_shared<AdmissionControlFilter>(config, "test_prefix.");
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void sampleCustomRequest(std::string&& http_error_code) {
    Http::TestResponseHeaderMapImpl headers{{":status", http_error_code}};
    filter_->encodeHeaders(headers, true);
  }

protected:
  std::string stats_prefix_{""};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::shared_ptr<AdmissionControlFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::shared_ptr<MockThreadLocalController> controller_;
  const std::string default_yaml_{R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
)EOF"};
};

TEST_F(AdmissionControlTest, FilterDisabled) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  // Fail lots of requests so that we would normally expect a ~100% rejection rate. It should pass
  // below since the filter is disabled.
  EXPECT_CALL(*controller_, requestTotalCount()).WillOnce(Return(1000));
  EXPECT_CALL(*controller_, requestSuccessCount()).WillOnce(Return(0));

  // We expect no rejections.
  Http::RequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdmissionControlTest, DisregardHealthChecks) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

  // Fail lots of requests so that we would normally expect a ~100% rejection rate. It should pass
  // below since the request is a healthcheck.
  EXPECT_CALL(*controller_, requestTotalCount()).WillOnce(Return(1000));
  EXPECT_CALL(*controller_, requestSuccessCount()).WillOnce(Return(0));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdmissionControlTest, FilterBehaviorBasic) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // Fail lots of requests so that we can expect a ~100% rejection rate.
  EXPECT_CALL(*controller_, requestTotalCount()).WillOnce(Return(1000));
  EXPECT_CALL(*controller_, requestSuccessCount()).WillOnce(Return(0));

  // We expect rejections due to the failure rate.
  EXPECT_EQ(0, scope_.counter("test_prefix.rq_rejected").value());
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, scope_.counter("test_prefix.rq_rejected").value());

  // Now we pretend as if the historical data has been phased out.

  // Should continue since SR has become stale and there's no additional data.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  // Fail exactly half of the requests so we get a ~50% rejection rate.
  EXPECT_CALL(*controller_, requestTotalCount()).WillOnce(Return(1000));
  EXPECT_CALL(*controller_, requestSuccessCount()).WillOnce(Return(500));

  // Random numbers in the range [0,1e4) are considered for the rejection calculation. One request
  // should fail and the other should pass.
  EXPECT_CALL(random_, random()).WillOnce(Return(5500));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(random_, random()).WillOnce(Return(4500));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

// Verify only 5xx codes count as errors.
TEST_F(AdmissionControlTest, ErrorCodes) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  EXPECT_CALL(*controller_, recordSuccess());
  sampleCustomRequest("200");

  EXPECT_CALL(*controller_, recordFailure());
  sampleCustomRequest("400");

  EXPECT_CALL(*controller_, recordFailure());
  sampleCustomRequest("500");
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
