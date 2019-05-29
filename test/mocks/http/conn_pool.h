#include <memory>

#include "envoy/http/conn_pool.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

namespace ConnectionPool {
class MockCancellable : public Cancellable {

public:
  MockCancellable();
  ~MockCancellable();

  // Http::ConnectionPool::Cancellable
  MOCK_METHOD0(cancel, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Http::ConnectionPool::Instance
  MOCK_CONST_METHOD0(protocol, Http::Protocol());
  MOCK_METHOD1(addDrainedCallback, void(DrainedCb cb));
  MOCK_METHOD0(drainConnections, void());
  MOCK_CONST_METHOD0(hasActiveConnections, bool());
  MOCK_METHOD2(newStream, Cancellable*(Http::StreamDecoder& response_decoder,
                                       Http::ConnectionPool::Callbacks& callbacks));
  MOCK_CONST_METHOD0(host, Upstream::HostDescriptionConstSharedPtr());

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_;
};

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
