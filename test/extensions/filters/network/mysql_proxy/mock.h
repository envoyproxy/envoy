#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/route.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MockClient : public Client {
public:
  MockClient() = default;
  ~MockClient() override = default;
  MOCK_METHOD(void, makeRequest, (Buffer::Instance&));
  MOCK_METHOD(void, close, ());
};

class MockDecoder : public Decoder {
public:
  MockDecoder(const MySQLSession& session);
  ~MockDecoder() override = default;
  MOCK_METHOD(void, onData, (Buffer::Instance & data));
  MOCK_METHOD(MySQLSession&, getSession, ());
  MySQLSession session_;
};

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
  MOCK_METHOD(RouteSharedPtr, upstreamPool, (const std::string&));
  RouteSharedPtr route;
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::ConnectionPoolManager* instance);
  ~MockRoute() override = default;
  MOCK_METHOD((ConnPool::ConnectionPoolManager&), upstream, ());
  MOCK_METHOD(void, test, (std::string &&));

  ConnPool::ConnectionPoolManager* pool;
};

class MockClientCallbacks : public ClientCallBack {
public:
  ~MockClientCallbacks() override = default;
  MOCK_METHOD(void, onResponse, (MySQLCodec&, uint8_t));
  MOCK_METHOD(void, onFailure, ());
};

namespace ConnPool {

class MockConnectionPoolManager : public ConnectionPoolManager {
public:
  ~MockConnectionPoolManager() override = default;
  MockConnectionPoolManager(const std::string& cluster_name) : cluster_name(cluster_name) {}
  MOCK_METHOD((Tcp::ConnectionPool::Cancellable*), newConnection, (ClientPoolCallBack&));
  std::string cluster_name;
};

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy