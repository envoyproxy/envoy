#include <memory>
#include <type_traits>

#include "envoy/buffer/buffer.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientImpl : public Client,
                   public DecoderCallbacks,
                   public Tcp::ConnectionPool::UpstreamCallbacks,
                   public Logger::Loggable<Logger::Id::client>,
                   public std::enable_shared_from_this<ClientImpl> {
public:
  ClientImpl(Tcp::ConnectionPool::ConnectionDataPtr&& conn, DecoderFactory& decoder_factory,
             ClientCallBack&);
  ~ClientImpl() override = default;
  // Client
  void makeRequest(Buffer::Instance& buffer) override;
  void close() override { conn_data_->connection().close(Network::ConnectionCloseType::NoFlush); }

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

  // UpstreamCallbacks
  void onUpstreamData(Buffer::Instance&, bool) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void setDecoderState(MySQLSession::State expected_state, uint8_t expected_seq);
  friend class ClientTest;

private:
  Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
  DecoderPtr decoder_;
  Buffer::OwnedImpl decode_buffer_;
  ClientCallBack& callbacks_;
};

class ClientFactoryImpl : public ClientFactory {
public:
  ClientPtr create(Tcp::ConnectionPool::ConnectionDataPtr&& conn, DecoderFactory& decoder_factory,
                   ClientCallBack&) override;

  static ClientFactoryImpl instance_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy