#pragma once

#include <memory>

#include "common/tcp_proxy/upstream.h"
#include "common/tcp_proxy/upstream_interface.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/tcp/mocks.h"

namespace Envoy {
namespace TcpProxy {
class MockGenericUpstream : public GenericUpstream {
public:
  MockGenericUpstream();
  ~MockGenericUpstream() override;
  MOCK_METHOD(bool, readDisable, (bool disable));
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, addBytesSentCallback, (Network::Connection::BytesSentCb cb));
  MOCK_METHOD(Tcp::ConnectionPool::ConnectionData*, onDownstreamEvent,
              (Network::ConnectionEvent event));
};

class MockGenericUpstreamPoolCallbacks : public GenericUpstreamPoolCallbacks {
public:
  MockGenericUpstreamPoolCallbacks();
  ~MockGenericUpstreamPoolCallbacks() override;
  MOCK_METHOD(void, onPoolFailure,
              (ConnectionPool::PoolFailureReason reason,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPoolReady,
              (const GenericUpstreamSharedPtr& upstream,
               Upstream::HostDescriptionConstSharedPtr& host,
               const Network::Address::InstanceConstSharedPtr& local_address,
               StreamInfo::StreamInfo& info));
};

class MockConnectionHandle : public ConnectionHandle {
public:
  MockConnectionHandle();
  ~MockConnectionHandle() override;
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(void, complete, ());
  MOCK_METHOD(GenericUpstreamSharedPtr, upstream, ());
  MOCK_METHOD(bool, failedOnPool, ());
  MOCK_METHOD(bool, failedOnConnection, ());
  MOCK_METHOD(bool, isConnecting, ());
};
} // namespace TcpProxy
} // namespace Envoy
