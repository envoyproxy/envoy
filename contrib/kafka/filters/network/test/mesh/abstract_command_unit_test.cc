#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (InFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class Testee : public BaseInFlightRequest {
public:
  Testee(AbstractRequestListener& filter) : BaseInFlightRequest{filter} {};
  void startProcessing() override { throw "not interesting"; }
  bool finished() const override { throw "not interesting"; }
  AbstractResponseSharedPtr computeAnswer() const override { throw "not interesting"; }
  void finishRequest() { BaseInFlightRequest::notifyFilter(); }
};

TEST(AbstractCommandTest, shouldNotifyFilter) {
  // given
  MockAbstractRequestListener filter;
  EXPECT_CALL(filter, onRequestReadyForAnswer());
  Testee testee = {filter};

  // when
  testee.finishRequest();

  // then - filter got notified that a requested has finished processing.
}

TEST(AbstractCommandTest, shouldHandleBeingAbandoned) {
  // given
  MockAbstractRequestListener filter;
  Testee testee = {filter};
  testee.abandon();

  // when, then - abandoned request does not notify filter even after it finishes.
  testee.finishRequest();
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
