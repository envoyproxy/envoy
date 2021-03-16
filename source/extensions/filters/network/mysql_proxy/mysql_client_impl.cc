#include "extensions/filters/network/mysql_proxy/mysql_client_impl.h"

#include "common/common/logger.h"

#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

ClientImpl::ClientImpl(ConnectionPool::ClientDataPtr&& conn, DecoderFactory& decoder_factory,
                       ClientCallBack& callbacks)
    : conn_data_(std::move(conn)), callbacks_(callbacks) {
  // start at command phase
  // server side decoder will only be called when server send response
  conn_data_->resetClient(decoder_factory.create(*this));
  conn_data_->decoder().getSession().setExpectedSeq(MYSQL_REQUEST_PKT_NUM + 1);
  conn_data_->decoder().getSession().setState(MySQLSession::State::ReqResp);
}

void ClientImpl::onProtocolError() { callbacks_.onFailure(); }

void ClientImpl::makeRequest(Buffer::Instance& buffer) {
  // downstream command will not pass server side decoder, change the decoder state to ensure
  // correction of state machine
  conn_data_->sendData(buffer);
  auto& decoder = conn_data_->decoder();
  decoder.getSession().setState(MySQLSession::State::ReqResp);
  decoder.getSession().setExpectedSeq(MYSQL_RESPONSE_PKT_NUM);
}

void ClientImpl::onCommandResponse(CommandResponse& resp) {
  auto seq = conn_data_->decoder().getSession().getExpectedSeq();
  callbacks_.onResponse(resp, seq - 1);
  conn_data_->decoder().getSession().setState(MySQLSession::State::ReqResp);
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(ConnectionPool::ClientDataPtr&& conn,
                                    DecoderFactory& decoder_factory, ClientCallBack& callbacks) {
  return std::make_unique<ClientImpl>(std::move(conn), decoder_factory, callbacks);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy