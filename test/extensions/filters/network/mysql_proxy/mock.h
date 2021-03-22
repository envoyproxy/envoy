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
  MockRoute(ConnectionPool::Instance* instance);
  ~MockRoute() override = default;
  MOCK_METHOD((ConnectionPool::Instance&), upstream, ());
  MOCK_METHOD(void, test, (std::string &&));

  ConnectionPool::Instance* pool;
};

class MockClientCallbacks : public ClientCallBack {
public:
  ~MockClientCallbacks() override = default;
  MOCK_METHOD(void, onResponse, (MySQLCodec&, uint8_t));
  MOCK_METHOD(void, onFailure, ());
};

namespace ConnectionPool {

class MockCancellable : public ConnectionPool::Cancellable {
public:
  ~MockCancellable() override = default;
  MOCK_METHOD(void, cancel, ());
};

class MockClientData : public ClientData {
public:
  MockClientData() = default;
  ~MockClientData() override = default;
  void resetClient(DecoderPtr&& decoder) override {
    decoder_ = std::move(decoder);
    resetClient_(decoder);
  }
  MOCK_METHOD(void, resetClient_, (DecoderPtr&));
  MOCK_METHOD(void, sendData, (Buffer::Instance&));
  MOCK_METHOD((Decoder&), decoder, ());
  MOCK_METHOD(void, close, ());
  DecoderPtr decoder_;
};

class MockClientPoolCallbacks : public ClientPoolCallBack {
public:
  ~MockClientPoolCallbacks() override = default;
  void onClientReady(ClientDataPtr&& data) override {
    data_ = std::move(data);
    onClientReady_(data);
  }
  MOCK_METHOD(void, onClientReady_, (ClientDataPtr&));
  /*
   * onClientFailure called when proxy failed to pass the connection phase
   */
  MOCK_METHOD(void, onClientFailure, (MySQLPoolFailureReason));
  ClientDataPtr data_;
};

class MockPool : public Instance {
public:
  MockPool() = default;
  MockPool(const std::string& name) : name(name) {}
  ~MockPool() override = default;
  MOCK_METHOD(Cancellable*, newMySQLClient, (ClientPoolCallBack&));
  std::string name;
};

} // namespace ConnectionPool

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy