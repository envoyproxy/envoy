#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientImpl : public Client, public DecoderCallbacks {
public:
  ClientImpl(ConnectionPool::ClientDataPtr&& conn, DecoderFactory& decoder_factory,
             ClientCallBack&);
  // Client
  void makeRequest(Buffer::Instance& buffer) override;
  void close() override { conn_data_->close(); }

  // DecoderCallbacks
  void onProtocolError() override;
  void onNewMessage(MySQLSession::State) override {}
  void onServerGreeting(ServerGreeting&) override {}
  void onClientLogin(ClientLogin&) override {}
  void onClientLoginResponse(ClientLoginResponse&) override {}
  void onClientSwitchResponse(ClientSwitchResponse&) override {}
  void onMoreClientLoginResponse(ClientLoginResponse&) override {}
  void onCommand(Command&) override {}
  void onCommandResponse(CommandResponse&) override;
  friend class ClientTest;

private:
  ConnectionPool::ClientDataPtr conn_data_;
  ClientCallBack& callbacks_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  ClientPtr create(ConnectionPool::ClientDataPtr&& conn, DecoderFactory& decoder_factory,
                   ClientCallBack&) override;

  static ClientFactoryImpl instance_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy