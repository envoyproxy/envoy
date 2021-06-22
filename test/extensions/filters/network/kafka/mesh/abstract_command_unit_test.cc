#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"

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
};

class MockUpstreamKafkaFacade : public UpstreamKafkaFacade {
public:
  MOCK_METHOD(RichKafkaProducer&, getProducerForTopic, (const std::string&));
};

class Testee : public BaseInFlightRequest {
public:
  Testee(AbstractRequestListener& filter) : BaseInFlightRequest{filter} {};
  void testFilterNotification() { BaseInFlightRequest::notifyFilter(); }
  void invoke(UpstreamKafkaFacade&) override { throw "not interesting"; }
  bool finished() const override { throw "not interesting"; }
  AbstractResponseSharedPtr computeAnswer() const override { throw "not interesting"; }
};

TEST(AbstractCommandTest, shouldHandleBeingAbandoned) {
  // given
  MockAbstractRequestListener filter;
  Testee testee = {filter};
  testee.abandon();

  // when, then - abandoned request does not notify filter even after it finishes.
  testee.testFilterNotification();
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
