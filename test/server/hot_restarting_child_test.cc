#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/server/hot_restarting_child.h"
#include "source/server/hot_restarting_parent.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_manager.h"
#include "test/server/hot_restart_udp_forwarding_test_helper.h"
#include "test/server/utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::AllOf;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WhenDynamicCastTo;

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class FakeHotRestartingParent : public HotRestartingBase {
public:
  FakeHotRestartingParent(Api::MockOsSysCalls& os_sys_calls, int base_id, int restart_epoch,
                          const std::string& socket_path)
      : HotRestartingBase(base_id), os_sys_calls_(os_sys_calls) {
    std::string socket_path_udp = socket_path + "_udp";
    main_rpc_stream_.bindDomainSocket(restart_epoch, "parent", socket_path, 0);
    udp_forwarding_rpc_stream_.bindDomainSocket(restart_epoch, "parent", socket_path_udp, 0);
    child_address_udp_forwarding_ = udp_forwarding_rpc_stream_.createDomainSocketAddress(
        restart_epoch + 1, "child", socket_path_udp, 0);
  }
  // Mocks the syscall for both send and receive, performs the send, and
  // triggers the child's callback to perform the receive.
  void sendUdpForwardingMessage(const envoy::HotRestartMessage& message) {
    auto buffer = std::make_shared<std::string>();
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _))
        .WillOnce([this, buffer](int, const msghdr* msg, int) {
          *buffer =
              std::string{static_cast<char*>(msg->msg_iov[0].iov_base), msg->msg_iov[0].iov_len};
          udp_file_ready_callback_(Event::FileReadyType::Read);
          return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
        });
    EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _)).WillRepeatedly([buffer](int, msghdr* msg, int) {
      if (buffer->empty()) {
        return Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN};
      }
      msg->msg_control = nullptr;
      msg->msg_controllen = 0;
      msg->msg_flags = 0;
      RELEASE_ASSERT(msg->msg_iovlen == 1,
                     fmt::format("recv buffer iovlen={}, expected 1", msg->msg_iovlen));
      size_t sz = std::min(buffer->size(), msg->msg_iov[0].iov_len);
      buffer->copy(static_cast<char*>(msg->msg_iov[0].iov_base), sz);
      *buffer = buffer->substr(sz);
      msg->msg_iov[0].iov_len = sz;
      return Api::SysCallSizeResult{static_cast<ssize_t>(sz), 0};
    });
    udp_forwarding_rpc_stream_.sendHotRestartMessage(child_address_udp_forwarding_, message);
  }
  Api::MockOsSysCalls& os_sys_calls_;
  Event::FileReadyCb udp_file_ready_callback_;
  sockaddr_un child_address_udp_forwarding_;
};

class HotRestartingChildTest : public testing::Test {
public:
  void SetUp() override {
    // Address-to-string conversion performs a socket call which we unfortunately
    // can't have bypass os_sys_calls_.
    EXPECT_CALL(os_sys_calls_, socket(_, _, _)).WillRepeatedly([this]() {
      static const int address_stringing_socket = 999;
      EXPECT_CALL(os_sys_calls_, close(address_stringing_socket))
          .WillOnce(Return(Api::SysCallIntResult{0, 0}));
      return Api::SysCallIntResult{address_stringing_socket, 0};
    });
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(4);
    EXPECT_CALL(os_sys_calls_, close(_)).Times(4);
    fake_parent_ = std::make_unique<FakeHotRestartingParent>(os_sys_calls_, 0, 0, socket_path_);
    hot_restarting_child_ = std::make_unique<HotRestartingChild>(0, 1, socket_path_, 0);
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, Event::FileReadyType::Read))
        .WillOnce(DoAll(SaveArg<1>(&fake_parent_->udp_file_ready_callback_), Return(nullptr)));
    hot_restarting_child_->initialize(dispatcher_);
  }
  void TearDown() override { hot_restarting_child_.reset(); }
  std::string socket_path_ = testDomainSocketName();
  Api::MockOsSysCalls os_sys_calls_;
  Event::MockDispatcher dispatcher_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  std::unique_ptr<FakeHotRestartingParent> fake_parent_;
  std::unique_ptr<HotRestartingChild> hot_restarting_child_;
};

TEST_F(HotRestartingChildTest, LogsErrorOnReplyMessageInUdpStream) {
  envoy::HotRestartMessage msg;
  msg.mutable_reply();
  EXPECT_LOG_CONTAINS(
      "error",
      "HotRestartMessage reply received on UdpForwarding (we want only requests); ignoring.",
      fake_parent_->sendUdpForwardingMessage(msg));
}

TEST_F(HotRestartingChildTest, LogsErrorOnNonUdpRelatedMessageInUdpStream) {
  envoy::HotRestartMessage msg;
  msg.mutable_request()->mutable_drain_listeners();
  EXPECT_LOG_CONTAINS(
      "error",
      "child sent a request other than ForwardedUdpPacket on udp forwarding socket; ignoring.",
      fake_parent_->sendUdpForwardingMessage(msg));
}

TEST_F(HotRestartingChildTest, DoesNothingOnForwardedUdpMessageWithNoMatchingListener) {
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  packet->set_local_addr("udp://127.0.0.1:1234");
  packet->set_peer_addr("udp://127.0.0.1:4321");
  packet->set_payload("hello");
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

TEST_F(HotRestartingChildTest, ExceptionOnUnparseablePeerAddress) {
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  packet->set_local_addr("udp://127.0.0.1:1234");
  packet->set_peer_addr("/tmp/domainsocket");
  packet->set_payload("hello");
  EXPECT_THROW(fake_parent_->sendUdpForwardingMessage(msg), EnvoyException);
}

TEST_F(HotRestartingChildTest, ExceptionOnUnparseableLocalAddress) {
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  packet->set_local_addr("/tmp/domainsocket");
  packet->set_peer_addr("udp://127.0.0.1:4321");
  packet->set_payload("hello");
  EXPECT_THROW(fake_parent_->sendUdpForwardingMessage(msg), EnvoyException);
}

MATCHER_P4(IsUdpWith, local_addr, peer_addr, buffer, timestamp, "") {
  bool local_matched = *arg.addresses_.local_ == *local_addr;
  if (!local_matched) {
    *result_listener << "\nUdpRecvData::addresses_.local_ == "
                     << Network::Utility::urlFromDatagramAddress(*arg.addresses_.local_)
                     << "\nexpected == " << Network::Utility::urlFromDatagramAddress(*local_addr);
  }
  bool peer_matched = *arg.addresses_.peer_ == *peer_addr;
  if (!peer_matched) {
    *result_listener << "\nUdpRecvData::addresses_.local_ == "
                     << Network::Utility::urlFromDatagramAddress(*arg.addresses_.peer_)
                     << "\nexpected == " << Network::Utility::urlFromDatagramAddress(*peer_addr);
  }
  std::string buffer_contents = arg.buffer_->toString();
  bool buffer_matched = buffer_contents == buffer;
  if (!buffer_matched) {
    *result_listener << "\nUdpRecvData::buffer_ contains " << buffer_contents << "\nexpected "
                     << buffer;
  }
  uint64_t ts =
      std::chrono::duration_cast<std::chrono::microseconds>(arg.receive_time_.time_since_epoch())
          .count();
  bool timestamp_matched = ts == timestamp;
  if (!timestamp_matched) {
    *result_listener << "\nUdpRecvData::received_time_ == " << ts << "\nexpected: " << timestamp;
  }
  return local_matched && peer_matched && buffer_matched && timestamp_matched;
}

TEST_F(HotRestartingChildTest, ForwardsPacketToRegisteredListenerOnMatch) {
  uint32_t worker_index = 12;
  uint64_t packet_timestamp = 987654321;
  std::string udp_contents = "beep boop";
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  auto mock_udp_listener_config = std::make_shared<Network::MockUdpListenerConfig>();
  auto test_listener_addr = Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  auto test_remote_addr = Network::Utility::resolveUrl("udp://127.0.0.1:4321");
  HotRestartUdpForwardingTestHelper(*hot_restarting_child_)
      .registerUdpForwardingListener(
          test_listener_addr,
          std::dynamic_pointer_cast<Network::UdpListenerConfig>(mock_udp_listener_config));
  packet->set_local_addr(Network::Utility::urlFromDatagramAddress(*test_listener_addr));
  packet->set_peer_addr(Network::Utility::urlFromDatagramAddress(*test_remote_addr));
  packet->set_worker_index(worker_index);
  packet->set_payload(udp_contents);
  packet->set_receive_time_epoch_microseconds(packet_timestamp);
  Network::MockUdpListenerWorkerRouter mock_worker_router;
  EXPECT_CALL(*mock_udp_listener_config,
              listenerWorkerRouter(WhenDynamicCastTo<const Network::Address::Ipv4Instance&>(
                  Eq(dynamic_cast<const Network::Address::Ipv4Instance&>(*test_listener_addr)))))
      .WillOnce(ReturnRef(mock_worker_router));
  EXPECT_CALL(mock_worker_router,
              deliver(worker_index, IsUdpWith(test_listener_addr, test_remote_addr, udp_contents,
                                              packet_timestamp)));
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

} // namespace
} // namespace Server
} // namespace Envoy
