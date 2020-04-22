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
  ~MockCancellable() override;

  // Http::ConnectionPool::Cancellable
  MOCK_METHOD(void, cancel, ());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // Http::ConnectionPool::Instance
  MOCK_METHOD(Http::Protocol, protocol, (), (const));
  MOCK_METHOD(void, addDrainedCallback, (DrainedCb cb));
  MOCK_METHOD(void, drainConnections, ());
  MOCK_METHOD(bool, hasActiveConnections, (), (const));
  MOCK_METHOD(Cancellable*, newStream, (ResponseDecoder & response_decoder, Callbacks& callbacks));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_;
};

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
