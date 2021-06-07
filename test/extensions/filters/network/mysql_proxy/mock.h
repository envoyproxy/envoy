#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_session.h"
#include "source/extensions/filters/network/mysql_proxy/route.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MockDecoder : public Decoder {
public:
  MockDecoder();
  ~MockDecoder() override = default;
  MOCK_METHOD(void, onData, (Buffer::Instance & data));
  MOCK_METHOD(MySQLSession&, getSession, ());
  std::unique_ptr<MySQLSession> session;
};

using MockDecoderPtr = std::unique_ptr<MockDecoder>;

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  ~MockDecoderCallbacks() override = default;
  MOCK_METHOD(void, onProtocolError, ());
  MOCK_METHOD(void, onNewMessage, (MySQLSession::State));
  MOCK_METHOD(void, onServerGreeting, (ServerGreeting&));
  MOCK_METHOD(void, onClientLogin, (ClientLogin&));
  MOCK_METHOD(void, onClientLoginResponse, (ClientLoginResponse&));
  MOCK_METHOD(void, onClientSwitchResponse, (ClientSwitchResponse&));
  MOCK_METHOD(void, onMoreClientLoginResponse, (ClientLoginResponse&));
  MOCK_METHOD(void, onCommand, (Command&));
  MOCK_METHOD(void, onCommandResponse, (CommandResponse&));
};

class MockRouter : public Router {
public:
  MockRouter(RouteSharedPtr route);
  ~MockRouter() override = default;
  MOCK_METHOD(RouteSharedPtr, upstreamPool, (const std::string&), (override));
  MOCK_METHOD(RouteSharedPtr, defaultPool, (), (override));
  RouteSharedPtr route;
};

class MockRoute : public Route {
public:
  MockRoute(Upstream::ThreadLocalCluster* instance, const std::string& name);
  ~MockRoute() override = default;
  MOCK_METHOD((Upstream::ThreadLocalCluster*), upstream, ());
  MOCK_METHOD((const std::string&), name, ());
  Upstream::ThreadLocalCluster* pool;
  const std::string& cluster_name;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
