#include "source/common/http/headers.h"
#include "source/extensions/filters/http/cache/thundering_herd_handler.h"

#include "test/extensions/filters/http/cache/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using ::testing::ReturnRef;

class ThunderingHerdHandlerTest : public Event::TestUsingSimulatedTime, public ::testing::Test {
public:
  ThunderingHerdHandlerTest()
      : api_(Api::createApiForTest(simTime())),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}
  void SetUp() override {
    EXPECT_CALL(mock_server_factory_context_, mainThreadDispatcher())
        .WillRepeatedly(ReturnRef(*dispatcher_));
    EXPECT_CALL(mock_decoder_callbacks_, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
    EXPECT_CALL(mock_decoder_callbacks_2_, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
    EXPECT_CALL(mock_decoder_callbacks_3_, dispatcher()).WillRepeatedly(ReturnRef(*dispatcher_));
  }

  void
  initHandler(const envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler&
                  config) {
    handler_ = ThunderingHerdHandler::create(config, mock_server_factory_context_);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Server::Configuration::MockServerFactoryContext mock_server_factory_context_;
  std::shared_ptr<MockThunderingHerdRetryInterface> mock_filter_ =
      std::make_shared<MockThunderingHerdRetryInterface>();
  std::shared_ptr<MockThunderingHerdRetryInterface> mock_filter_2_ =
      std::make_shared<MockThunderingHerdRetryInterface>();
  std::shared_ptr<MockThunderingHerdRetryInterface> mock_filter_3_ =
      std::make_shared<MockThunderingHerdRetryInterface>();
  Http::MockStreamDecoderFilterCallbacks mock_decoder_callbacks_;
  Http::MockStreamDecoderFilterCallbacks mock_decoder_callbacks_2_;
  Http::MockStreamDecoderFilterCallbacks mock_decoder_callbacks_3_;
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {":scheme", "https"}, {":host", "test_host"}};
  Key key_;
  // handler_ must be initialized at the start of each test.
  std::shared_ptr<ThunderingHerdHandler> handler_;

  void makeThreeRequests() {
    handler_->handleUpstreamRequest(mock_filter_, &mock_decoder_callbacks_, key_, request_headers_);
    handler_->handleUpstreamRequest(mock_filter_2_, &mock_decoder_callbacks_2_, key_,
                                    request_headers_);
    handler_->handleUpstreamRequest(mock_filter_3_, &mock_decoder_callbacks_3_, key_,
                                    request_headers_);
  }
};

TEST_F(ThunderingHerdHandlerTest, WriteFailedWithNoBlockedRequestsDoesNothing) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  initHandler(config);
  // Make one request.
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  handler_->handleUpstreamRequest(mock_filter_, &mock_decoder_callbacks_, key_, request_headers_);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  // Nothing additional is expected to happen - the queue should have been released,
  // and since nothing was blocked, nothing is unblocked.
}

TEST_F(ThunderingHerdHandlerTest, UnsetIsPassThrough) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  EXPECT_CALL(mock_decoder_callbacks_2_, continueDecoding());
  EXPECT_CALL(mock_decoder_callbacks_3_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_2_);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_3_);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
}

TEST_F(ThunderingHerdHandlerTest, DefaultBlockUntilCompletionBlocksAfterOneAndReleasesOneOnFail) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  config.mutable_block_until_completion();
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  // Failure of the first filter should cause the second filter to continueDecoding, via dispatcher.
  EXPECT_CALL(mock_decoder_callbacks_2_, continueDecoding());
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_2_);
  // Failure of the second filter should cause the third filter to continueDecoding, via dispatcher.
  EXPECT_CALL(mock_decoder_callbacks_3_, continueDecoding());
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_3_);
  // Failure of the third filter should clear the hash because there are no more blockers.
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
}

TEST_F(ThunderingHerdHandlerTest, DefaultBlockUntilCompletionRetriesAllOnSuccess) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  config.mutable_block_until_completion();
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  // Success of the first filter should cause the other filters to retryHeaders, via dispatchers.
  EXPECT_CALL(*mock_filter_2_, retryHeaders(_));
  EXPECT_CALL(*mock_filter_3_, retryHeaders(_));
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Inserted);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(ThunderingHerdHandlerTest, DefaultBlockUntilCompletionSkipsOneOnFilterDeletion) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  config.mutable_block_until_completion();
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  // Failure of the first filter should dispatch request for next filter to continue,
  // abort it because the filter is deleted, and dispatch request for *third* filter.
  mock_filter_2_.reset();
  EXPECT_CALL(mock_decoder_callbacks_3_, continueDecoding());
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_F(ThunderingHerdHandlerTest, BlockUntilCompletionAllowsThroughSpecifiedNumberOfRequests) {
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  config.mutable_block_until_completion()->set_parallel_requests(2);
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  EXPECT_CALL(mock_decoder_callbacks_2_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_2_);
  // Failure of the first filter should cause the remaining blocked filter to start, via
  // dispatchers.
  EXPECT_CALL(mock_decoder_callbacks_3_, continueDecoding());
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_3_);
  // Failure and success of the second and third filter should do nothing, as nothing else is
  // blocked.
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Inserted);
}

TEST_F(ThunderingHerdHandlerTest, UnblockPeriodTimeoutReleasesAnotherRequest) {
  constexpr uint32_t period_ms = 1000;
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  *config.mutable_block_until_completion()->mutable_unblock_additional_request_period() =
      ProtobufUtil::TimeUtil::MillisecondsToDuration(period_ms);
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  // Nothing should happen if we run the dispatcher now.
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // But if we advance time, the next handler should be released.
  EXPECT_CALL(mock_decoder_callbacks_2_, continueDecoding());
  simTime().advanceTimeAndRun(std::chrono::milliseconds(period_ms), *dispatcher_,
                              Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_2_);
  // And if we do the same again, the final handler should be released.
  EXPECT_CALL(mock_decoder_callbacks_3_, continueDecoding());
  simTime().advanceTimeAndRun(std::chrono::milliseconds(period_ms), *dispatcher_,
                              Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_3_);
  // Failure or success of the three instances now all running should do nothing.
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Failed);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Inserted);
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Inserted);
}

TEST_F(ThunderingHerdHandlerTest, UnblockPeriodTimeoutDoesNotRunIfRequestAlreadyCompleted) {
  constexpr uint32_t period_ms = 1000;
  envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler config;
  *config.mutable_block_until_completion()->mutable_unblock_additional_request_period() =
      ProtobufUtil::TimeUtil::MillisecondsToDuration(period_ms);
  initHandler(config);
  EXPECT_CALL(mock_decoder_callbacks_, continueDecoding());
  makeThreeRequests();
  ::testing::Mock::VerifyAndClearExpectations(&mock_decoder_callbacks_);
  // Complete the first request as success.
  // Success of the first filter should cause the other filters to retryHeaders, via dispatchers.
  EXPECT_CALL(*mock_filter_2_, retryHeaders(_));
  EXPECT_CALL(*mock_filter_3_, retryHeaders(_));
  handler_->handleInsertFinished(key_, ThunderingHerdHandler::InsertResult::Inserted);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ::testing::Mock::VerifyAndClearExpectations(&*mock_filter_2_);
  ::testing::Mock::VerifyAndClearExpectations(&*mock_filter_3_);
  // Nothing should happen if we advance time now.
  simTime().advanceTimeAndRun(std::chrono::milliseconds(period_ms), *dispatcher_,
                              Event::Dispatcher::RunType::Block);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
