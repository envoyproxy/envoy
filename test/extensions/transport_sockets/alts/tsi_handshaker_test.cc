#include "extensions/transport_sockets/alts/tsi_handshaker.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/tsi/fake_transport_security.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::SaveArg;
using testing::Test;
using testing::_;

class MockTsiHandshakerCallbacks : public TsiHandshakerCallbacks {
public:
  void onNextDone(NextResultPtr&& result) override { onNextDone_(result.get()); }
  MOCK_METHOD1(onNextDone_, void(NextResult*));

  void expectDone(tsi_result status, Buffer::Instance& to_send, CHandshakerResultPtr& result) {
    EXPECT_CALL(*this, onNextDone_(_))
        .WillOnce(Invoke([&, status](TsiHandshakerCallbacks::NextResult* next_result) {
          EXPECT_EQ(status, next_result->status_);
          to_send.add(*next_result->to_send_);
          result.swap(next_result->result_);
        }));
  }
};

class TsiHandshakerTest : public Test {
public:
  TsiHandshakerTest()
      : server_handshaker_({tsi_create_fake_handshaker(0)}, dispatcher_),
        client_handshaker_({tsi_create_fake_handshaker(1)}, dispatcher_) {
    server_handshaker_.setHandshakerCallbacks(server_callbacks_);
    client_handshaker_.setHandshakerCallbacks(client_callbacks_);
  }

protected:
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockTsiHandshakerCallbacks server_callbacks_;
  MockTsiHandshakerCallbacks client_callbacks_;
  TsiHandshaker server_handshaker_;
  TsiHandshaker client_handshaker_;
};

TEST_F(TsiHandshakerTest, DoHandshake) {
  InSequence s;

  Buffer::OwnedImpl server_sent;
  Buffer::OwnedImpl client_sent;

  CHandshakerResultPtr client_result;
  CHandshakerResultPtr server_result;

  client_callbacks_.expectDone(TSI_OK, client_sent, client_result);
  client_handshaker_.next(server_sent); // Initially server_sent is empty.
  EXPECT_EQ(nullptr, client_result);
  EXPECT_EQ("CLIENT_INIT", client_sent.toString().substr(4));

  server_callbacks_.expectDone(TSI_OK, server_sent, server_result);
  server_handshaker_.next(client_sent);
  EXPECT_EQ(nullptr, client_result);
  EXPECT_EQ("SERVER_INIT", server_sent.toString().substr(4));

  client_callbacks_.expectDone(TSI_OK, client_sent, client_result);
  client_handshaker_.next(server_sent);
  EXPECT_EQ(nullptr, client_result);
  EXPECT_EQ("CLIENT_FINISHED", client_sent.toString().substr(4));

  server_callbacks_.expectDone(TSI_OK, server_sent, server_result);
  server_handshaker_.next(client_sent);
  EXPECT_NE(nullptr, server_result);
  EXPECT_EQ("SERVER_FINISHED", server_sent.toString().substr(4));

  client_callbacks_.expectDone(TSI_OK, client_sent, client_result);
  client_handshaker_.next(server_sent);
  EXPECT_NE(nullptr, client_result);
  EXPECT_EQ("", client_sent.toString());

  tsi_peer client_peer;
  EXPECT_EQ(TSI_OK, tsi_handshaker_result_extract_peer(client_result.get(), &client_peer));
  EXPECT_EQ(1, client_peer.property_count);
  EXPECT_STREQ("certificate_type", client_peer.properties[0].name);
  absl::string_view client_certificate_type{client_peer.properties[0].value.data,
                                            client_peer.properties[0].value.length};
  EXPECT_EQ("FAKE", client_certificate_type);

  tsi_peer server_peer;
  EXPECT_EQ(TSI_OK, tsi_handshaker_result_extract_peer(server_result.get(), &server_peer));
  EXPECT_EQ(1, server_peer.property_count);
  EXPECT_STREQ("certificate_type", server_peer.properties[0].name);
  absl::string_view server_certificate_type{server_peer.properties[0].value.data,
                                            server_peer.properties[0].value.length};
  EXPECT_EQ("FAKE", server_certificate_type);

  tsi_peer_destruct(&client_peer);
  tsi_peer_destruct(&server_peer);
}

TEST_F(TsiHandshakerTest, IncompleteData) {
  InSequence s;

  Buffer::OwnedImpl server_sent;
  Buffer::OwnedImpl client_sent;

  CHandshakerResultPtr client_result;
  CHandshakerResultPtr server_result;

  client_callbacks_.expectDone(TSI_OK, client_sent, client_result);
  client_handshaker_.next(server_sent); // Initially server_sent is empty.
  EXPECT_EQ(nullptr, client_result);
  EXPECT_EQ("CLIENT_INIT", client_sent.toString().substr(4));

  client_sent.drain(3); // make data incomplete
  server_callbacks_.expectDone(TSI_INCOMPLETE_DATA, server_sent, server_result);
  server_handshaker_.next(client_sent);
  EXPECT_EQ(nullptr, client_result);
  EXPECT_EQ("", server_sent.toString());
}

TEST_F(TsiHandshakerTest, DeferredDelete) {
  InSequence s;

  TsiHandshakerPtr handshaker{new TsiHandshaker({tsi_create_fake_handshaker(0)}, dispatcher_)};
  handshaker->deferredDelete();
  // The handshaker is now in dispatcher_ to delete queue.
  EXPECT_EQ(dispatcher_.to_delete_.back().get(), handshaker.get());
  handshaker.release();
}

TEST_F(TsiHandshakerTest, DeleteOnDone) {
  InSequence s;

  TsiHandshakerPtr handshaker(new TsiHandshaker({tsi_create_fake_handshaker(1)}, dispatcher_));
  handshaker->setHandshakerCallbacks(client_callbacks_);

  Buffer::OwnedImpl empty;
  std::function<void()> done;

  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&done));

  handshaker->next(empty);
  handshaker->deferredDelete();

  // Make sure the handshaker is not in dispatcher_ queue, since the next call is not done.
  EXPECT_NE(dispatcher_.to_delete_.back().get(), handshaker.get());

  // After deferredDelete, the callback should be never invoked, in real use it might be already
  // a dangling pointer.
  EXPECT_CALL(client_callbacks_, onNextDone_(_)).Times(0);

  // Simulate the next call is completed.
  done();

  // The handshaker is now in dispatcher_ to delete queue.
  EXPECT_EQ(dispatcher_.to_delete_.back().get(), handshaker.get());
  handshaker.release();
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
