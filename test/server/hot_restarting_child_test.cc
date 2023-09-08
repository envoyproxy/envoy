#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/server/hot_restarting_child.h"
#include "source/server/hot_restarting_parent.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_manager.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

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
          std::cerr << "calling callback" << std::endl;
          udp_file_ready_callback_(Event::FileReadyType::Read);
          std::cerr << "called callback" << std::endl;
          return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
        });
    EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _)).WillRepeatedly([buffer](int, msghdr* msg, int) {
      if (buffer->empty()) {
        return Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN};
      }
      std::cerr << "mock recv" << std::endl;
      msg->msg_control = nullptr;
      msg->msg_controllen = 0;
      msg->msg_flags = 0;
      std::cerr << "mock recv 2" << std::endl;
      RELEASE_ASSERT(msg->msg_iovlen == 1,
                     fmt::format("recv buffer iovlen={}, expected 1", msg->msg_iovlen));
      size_t sz = std::min(buffer->size(), msg->msg_iov[0].iov_len);
      buffer->copy(static_cast<char*>(msg->msg_iov[0].iov_base), sz);
      *buffer = buffer->substr(sz);
      msg->msg_iov[0].iov_len = sz;
      std::cerr << "mock recv 3" << std::endl;
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
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(4);
    EXPECT_CALL(os_sys_calls_, close(_)).Times(4);
    fake_parent_ = std::make_unique<FakeHotRestartingParent>(os_sys_calls_, 0, 0, socket_path_);
    hot_restarting_child_ = std::make_unique<HotRestartingChild>(0, 1, socket_path_, 0);
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, Event::FileReadyType::Read))
        .WillOnce(DoAll(SaveArg<1>(&fake_parent_->udp_file_ready_callback_), Return(nullptr)));
    hot_restarting_child_->initialize(dispatcher_);
  }
  void TearDown() override { hot_restarting_child_.reset(); }
  std::string socket_path_{"@envoy_domain_socket"};
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
  packet->set_local_addr("127.0.0.1:1234");
  packet->set_peer_addr("127.0.0.1:4321");
  packet->set_packet("hello");
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

TEST_F(HotRestartingChildTest, ForwardsPacketToListenerOnMatch) {
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  // TODO(ravenblack): once #29137 is in, add an address to the map.
  //
  packet->set_local_addr("127.0.0.1:1234");
  packet->set_peer_addr("127.0.0.1:4321");
  packet->set_packet("hello");
  // TODO(ravenblack): once #29137 is in:
  // EXPECT_CALL(mock_listener, listenerWorkerRouter(addr)).WillOnce(Return(mock_worker_router));
  // EXPECT_CALL(mock_worker_router, deliver(worker_index, expected_udp_recv_data));
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

} // namespace
} // namespace Server
} // namespace Envoy
