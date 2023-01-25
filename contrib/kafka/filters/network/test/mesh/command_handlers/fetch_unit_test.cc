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
  MOCK_METHOD(std::vector<FetchableTopicResponse>, convert, (const INPUT&), (const));
};

class FetchUnitTest : public testing::Test {
protected:
  NiceMock<MockAbstractRequestListener> filter_;
  Event::MockDispatcher dispatcher_;
  MockRecordCallbackProcessor callback_processor_;
  MockFetchRecordConverter converter_;

  FetchUnitTest() { ON_CALL(filter_, dispatcher).WillByDefault(ReturnRef(dispatcher_)); }

  std::shared_ptr<FetchRequestHolder> makeTestee() {
    const RequestHeader header = {0, 0, 0, absl::nullopt};
    const FetchRequest data = {0, 0, 0, {}};
    const auto message = std::make_shared<Request<FetchRequest>>(header, data);
    return std::make_shared<FetchRequestHolder>(filter_, callback_processor_, message, converter_);
  }
};

TEST_F(FetchUnitTest, shouldRegisterCallbackAndTimer) {
  auto testee = makeTestee();
  EXPECT_CALL(callback_processor_, processCallback(_));
  EXPECT_CALL(dispatcher_, createTimer_(_));
  testee->startProcessing();
}

// interest

// markFinishedByTimer

// markFinishedByTimer x2

// receive (more)

// receive (finish)

// receive (reject)

// abandon

// computeAnswer

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
