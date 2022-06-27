#pragma once

#include <memory>

#include "envoy/http/conn_pool.h"

#include "test/mocks/common.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

class MockCallbacks : public Callbacks {
  MOCK_METHOD(void, onPoolFailure,
              (PoolFailureReason reason, absl::string_view transport_failure_reason,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPoolReady,
              (RequestEncoder & encoder, Upstream::HostDescriptionConstSharedPtr host,
               StreamInfo::StreamInfo& info, absl::optional<Http::Protocol> protocol));
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // Http::ConnectionPool::Instance
  MOCK_METHOD(Http::Protocol, protocol, (), (const));
  MOCK_METHOD(void, addIdleCallback, (IdleCb cb));
  MOCK_METHOD(bool, isIdle, (), (const));
  MOCK_METHOD(void, drainConnections, (Envoy::ConnectionPool::DrainBehavior drain_behavior));
  MOCK_METHOD(bool, hasActiveConnections, (), (const));
  MOCK_METHOD(Cancellable*, newStream,
              (ResponseDecoder & response_decoder, Callbacks& callbacks,
               const Instance::StreamOptions&));
  MOCK_METHOD(bool, maybePreconnect, (float));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, host, (), (const));
  MOCK_METHOD(absl::string_view, protocolDescription, (), (const));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_;
  IdleCb idle_cb_;
};

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
