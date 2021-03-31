#include "extensions/filters/network/mysql_proxy/mysql_client_impl.h"

#include "envoy/network/connection.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

ClientImpl::ClientImpl(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                       DecoderFactory& decoder_factory, ClientCallBack& callbacks)
    : conn_data_(std::move(conn)), callbacks_(callbacks) {

  conn_data_->addUpstreamCallbacks(*this);
  conn_data_->connection().readDisable(false);
  decoder_ = decoder_factory.create(*this);
  decoder_->getSession().setExpectedSeq(MYSQL_REQUEST_PKT_NUM + 1);
  decoder_->getSession().setState(MySQLSession::State::ReqResp);
}

void ClientImpl::onProtocolError() {
  ENVOY_CONN_LOG(error, "protocol error of upstream client ", conn_data_->connection());
  callbacks_.onFailure();
}

void ClientImpl::onCommandResponse(CommandResponse& resp) {
  callbacks_.onResponse(resp, decoder_->getSession().getExpectedSeq() - 1);
  decoder_->getSession().setState(MySQLSession::State::ReqResp);
}

void ClientImpl::makeRequest(Buffer::Instance& buffer) {
  // downstream command will not pass server side decoder, change the decoder state to ensure
  // correction of state machine
  ENVOY_CONN_LOG(debug, "write buffer to upstream, len {}", conn_data_->connection(),
                 buffer.length());
  conn_data_->connection().write(buffer, false);
  decoder_->getSession().setState(MySQLSession::State::ReqResp);
  decoder_->getSession().setExpectedSeq(MYSQL_RESPONSE_PKT_NUM);
}

void ClientImpl::onUpstreamData(Buffer::Instance& buffer, bool) {
  ENVOY_LOG(debug, "mysql client recevied upstream data, len {}", buffer.length());
  decode_buffer_.move(buffer);
  decoder_->onData(decode_buffer_);
}

void ClientImpl::onEvent(Network::ConnectionEvent event) {
  ASSERT(event != Network::ConnectionEvent::Connected);
  if (event == Network::ConnectionEvent::RemoteClose) {
    ENVOY_LOG(debug, "mysql client, closed by remote");
    return;
  }
  if (event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(debug, "mysql client, closed by local");
  }
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                                    DecoderFactory& decoder_factory, ClientCallBack& callbacks) {
  return std::make_unique<ClientImpl>(std::move(conn), decoder_factory, callbacks);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy