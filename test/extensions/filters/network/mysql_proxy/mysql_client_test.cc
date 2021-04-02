

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_client_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

#include "test/mocks/tcp/mocks.h"

#include "gtest/gtest.h"
#include "mock.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class ClientTest : public testing::Test, public DecoderFactory {
public:
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return DecoderPtr(decoder_);
  }

  void setup() {
    EXPECT_CALL(*decoder_, getSession()).Times(2).WillRepeatedly(ReturnRef(decoder_->session_));
    EXPECT_CALL(data_, addUpstreamCallbacks(_));
    EXPECT_CALL(data_, connection()).WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, readDisable(false));

    auto data = Tcp::ConnectionPool::ConnectionDataPtr{&data_};
    client_ = std::make_unique<ClientImpl>(std::move(data), *this, client_callbacks_);
    EXPECT_EQ(decoder_->session_.getExpectedSeq(), MYSQL_REQUEST_PKT_NUM + 1);
    EXPECT_EQ(decoder_->session_.getState(), MySQLSession::State::ReqResp);
  }

  MySQLSession session_;
  MockDecoder* decoder_{new MockDecoder(session_)};
  Tcp::ConnectionPool::MockConnectionData data_;
  Network::MockClientConnection connection_;
  DecoderCallbacks* decoder_callbacks_;
  MockClientCallbacks client_callbacks_;
  std::unique_ptr<ClientImpl> client_;
};

TEST_F(ClientTest, SendMessage) {
  setup();
  EXPECT_CALL(data_, connection()).WillOnce(ReturnRef(connection_));
  EXPECT_CALL(connection_, write(_, false));
  EXPECT_CALL(*decoder_, getSession()).Times(2).WillRepeatedly(ReturnRef(decoder_->session_));
  Buffer::OwnedImpl buffer;
  client_->makeRequest(buffer);
  EXPECT_EQ(decoder_->session_.getExpectedSeq(), MYSQL_RESPONSE_PKT_NUM);
  EXPECT_EQ(decoder_->session_.getState(), MySQLSession::State::ReqResp);
}

TEST_F(ClientTest, ProtocolErr) {
  setup();
  EXPECT_CALL(client_callbacks_, onFailure());
  client_->onProtocolError();
}

TEST_F(ClientTest, OnCommandResponse) {
  setup();
  auto session = decoder_->session_;
  EXPECT_CALL(*decoder_, getSession()).Times(2).WillRepeatedly(ReturnRef(session));
  EXPECT_CALL(client_callbacks_, onResponse(_, session.getExpectedSeq() - 1));
  CommandResponse resp{};
  client_->onCommandResponse(resp);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy