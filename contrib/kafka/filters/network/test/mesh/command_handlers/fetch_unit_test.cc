#include "test/mocks/event/mocks.h"

#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch_record_converter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (InFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class MockRecordCallbackProcessor : public RecordCallbackProcessor {
public:
  MOCK_METHOD(void, processCallback, (const RecordCbSharedPtr&));
  MOCK_METHOD(void, removeCallback, (const RecordCbSharedPtr&));
};

class MockFetchRecordConverter : public FetchRecordConverter {
public:
  MOCK_METHOD(std::vector<FetchableTopicResponse>, convert, (const InboundRecordsMap&), (const));
};

class FetchUnitTest : public testing::Test {
protected:
  constexpr static int64_t TEST_CORRELATION_ID = 123456;

  NiceMock<MockAbstractRequestListener> filter_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockRecordCallbackProcessor> callback_processor_;
  MockFetchRecordConverter converter_;

  FetchUnitTest() { ON_CALL(filter_, dispatcher).WillByDefault(ReturnRef(dispatcher_)); }

  std::shared_ptr<FetchRequestHolder> makeTestee() {
    const RequestHeader header = {FETCH_REQUEST_API_KEY, 0, TEST_CORRELATION_ID, absl::nullopt};
    // Our request refers to aaa-0, aaa-1, bbb-10, bbb-20.
    const FetchTopic t1 = {"aaa", {{0, 0, 0}, {1, 0, 0}}};
    const FetchTopic t2 = {"bbb", {{10, 0, 0}, {20, 0, 0}}};
    const FetchRequest data = {0, 0, 0, {t1, t2}};
    const auto message = std::make_shared<Request<FetchRequest>>(header, data);
    return std::make_shared<FetchRequestHolder>(filter_, callback_processor_, message, converter_);
  }
};

TEST_F(FetchUnitTest, ShouldRegisterCallbackAndTimer) {
  // given
  const auto testee = makeTestee();
  EXPECT_CALL(callback_processor_, processCallback(_));
  EXPECT_CALL(dispatcher_, createTimer_(_));

  // when
  testee->startProcessing();

  // then
  ASSERT_FALSE(testee->finished());
}

TEST_F(FetchUnitTest, ShouldReturnProperInterest) {
  // given
  const auto testee = makeTestee();

  // when
  const TopicToPartitionsMap result = testee->interest();

  // then
  const TopicToPartitionsMap expected = {{"aaa", {0, 1}}, {"bbb", {10, 20}}};
  ASSERT_EQ(result, expected);
}

TEST_F(FetchUnitTest, ShouldCleanupAfterTimer) {
  // given
  const auto testee = makeTestee();
  testee->startProcessing();

  EXPECT_CALL(callback_processor_, removeCallback(_));
  EXPECT_CALL(dispatcher_, post(_));

  // when
  testee->markFinishedByTimer();

  // then
  ASSERT_TRUE(testee->finished());
}

// Helper method to generate records.
InboundRecordSharedPtr makeRecord() {
  return std::make_shared<InboundRecord>("aaa", 0, 0, absl::nullopt, absl::nullopt);
}

TEST_F(FetchUnitTest, ShouldReceiveRecords) {
  // given
  const auto testee = makeTestee();
  testee->startProcessing();

  // Will be invoked by the third record (delivery was finished).
  EXPECT_CALL(dispatcher_, post(_));
  // It is invoker that removes the callback - not us.
  EXPECT_CALL(callback_processor_, removeCallback(_)).Times(0);

  // when - 1
  const auto res1 = testee->receive(makeRecord());
  // then - first record got stored.
  ASSERT_EQ(res1, CallbackReply::AcceptedAndWantMore);
  ASSERT_FALSE(testee->finished());

  // when - 2
  const auto res2 = testee->receive(makeRecord());
  // then - second record got stored.
  ASSERT_EQ(res2, CallbackReply::AcceptedAndWantMore);
  ASSERT_FALSE(testee->finished());

  // when - 3
  const auto res3 = testee->receive(makeRecord());
  // then - third record got stored and no more will be accepted.
  ASSERT_EQ(res3, CallbackReply::AcceptedAndFinished);
  ASSERT_TRUE(testee->finished());

  // when - 4
  const auto res4 = testee->receive(makeRecord());
  // then - fourth record was rejected.
  ASSERT_EQ(res4, CallbackReply::Rejected);
}

TEST_F(FetchUnitTest, ShouldRejectRecordsAfterTimer) {
  // given
  const auto testee = makeTestee();
  testee->startProcessing();
  testee->markFinishedByTimer();

  // when
  const auto res = testee->receive(makeRecord());

  // then
  ASSERT_EQ(res, CallbackReply::Rejected);
}

TEST_F(FetchUnitTest, ShouldUnregisterItselfWhenAbandoned) {
  // given
  const auto testee = makeTestee();
  testee->startProcessing();

  EXPECT_CALL(callback_processor_, removeCallback(_));

  // when
  testee->abandon();

  // then - expectations are met.
}

TEST_F(FetchUnitTest, ShouldComputeAnswer) {
  // given
  const auto testee = makeTestee();
  testee->startProcessing();

  std::vector<FetchableTopicResponse> ftr = {{"aaa", {}}, {"bbb", {}}};
  EXPECT_CALL(converter_, convert(_)).WillOnce(Return(ftr));

  // when
  const AbstractResponseSharedPtr answer = testee->computeAnswer();

  // then
  ASSERT_EQ(answer->metadata_.correlation_id_, TEST_CORRELATION_ID);
  const auto response = std::dynamic_pointer_cast<Response<FetchResponse>>(answer);
  ASSERT_TRUE(response);
  const std::vector<FetchableTopicResponse> responses = response->data_.responses_;
  ASSERT_EQ(responses, ftr);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
