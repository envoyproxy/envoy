#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/server/hot_restarting_child.h"
#include "source/server/hot_restarting_parent.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/server/hot_restart_udp_forwarding_test_helper.h"
#include "test/server/utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
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
          THROW_IF_NOT_OK(udp_file_ready_callback_(Event::FileReadyType::Read));
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
  void expectParentTerminateMessages() {
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
      return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
    });
  }
  // The wire form of a message as one datagram: uint64 'length' then the serialized proto.
  static std::string serialize(const envoy::HotRestartMessage& message) {
    std::string serialized;
    RELEASE_ASSERT(message.SerializeToString(&serialized), "");
    uint64_t length = htobe64(serialized.size());
    std::string wire(reinterpret_cast<const char*>(&length), sizeof(length));
    return wire + serialized;
  }
  // Copies as much of `wire` as fits into the receive buffer of `msg`, as a datagram would.
  static Api::SysCallSizeResult deliver(std::string& wire, msghdr* msg) {
    msg->msg_control = nullptr;
    msg->msg_controllen = 0;
    msg->msg_flags = 0;
    RELEASE_ASSERT(msg->msg_iovlen == 1, "");
    size_t sz = std::min(wire.size(), msg->msg_iov[0].iov_len);
    wire.copy(static_cast<char*>(msg->msg_iov[0].iov_base), sz);
    wire = wire.substr(sz);
    return Api::SysCallSizeResult{static_cast<ssize_t>(sz), 0};
  }
  static Api::SysCallSizeResult wouldBlock() { return Api::SysCallSizeResult{-1, EAGAIN}; }
  static envoy::HotRestartMessage::Request::RequestCase requestCase(const msghdr* msg) {
    envoy::HotRestartMessage sent;
    RELEASE_ASSERT(msg->msg_iov[0].iov_len > sizeof(uint64_t), "");
    RELEASE_ASSERT(
        sent.ParseFromArray(static_cast<uint8_t*>(msg->msg_iov[0].iov_base) + sizeof(uint64_t),
                            msg->msg_iov[0].iov_len - sizeof(uint64_t)),
        "");
    return sent.request().request_case();
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
    // Send/receive timeouts are set on each socket at bind time.
    EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, _, _, _)).Times(AnyNumber());
    fake_parent_ = std::make_unique<FakeHotRestartingParent>(os_sys_calls_, 0, 0, socket_path_);
    hot_restarting_child_ = std::make_unique<HotRestartingChild>(
        0, 1, socket_path_, 0, skipHotRestartOnNoParent(), skipParentStats());
    if (skipHotRestartOnNoParent()) {
      if (hotRestartIsSkipped()) {
        // A message is attempted to be sent to the parent and returns ECONNREFUSED.
        EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr*, int) {
          return Api::SysCallSizeResult{0, ECONNREFUSED};
        });
      } else {
        // Should be a message sent to the parent that does nothing.
        EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
          return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
        });
      }
    }
    if (!hotRestartIsSkipped()) {
      EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, Event::FileReadyType::Read))
          .WillOnce(DoAll(SaveArg<1>(&fake_parent_->udp_file_ready_callback_), Return(nullptr)));
    }
    hot_restarting_child_->initialize(dispatcher_);
  }
  void TearDown() override { hot_restarting_child_.reset(); }
  std::string socket_path_ = testDomainSocketName();
  Api::MockOsSysCalls os_sys_calls_;
  Event::MockDispatcher dispatcher_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  std::unique_ptr<FakeHotRestartingParent> fake_parent_;
  std::unique_ptr<HotRestartingChild> hot_restarting_child_;
  virtual bool skipHotRestartOnNoParent() const { return false; }
  virtual bool hotRestartIsSkipped() const { return false; }
  virtual bool skipParentStats() const { return false; }
};

class HotRestartingChildWithSkipTest : public HotRestartingChildTest {
public:
  bool skipHotRestartOnNoParent() const override { return true; }
  bool hotRestartIsSkipped() const override { return false; }
};

class HotRestartingChildWithSkipAndNoParentTest : public HotRestartingChildWithSkipTest {
public:
  bool skipHotRestartOnNoParent() const override { return true; }
  bool hotRestartIsSkipped() const override { return true; }
};

TEST_F(HotRestartingChildWithSkipTest, BehavesNormallyIfParentConnectWorked) {
  // the mock expectations perform all actions of this test
}

TEST_F(HotRestartingChildWithSkipAndNoParentTest, SkipsOtherActionsIfParentConnectFailed) {
  // the mock expectations perform all actions of this test
}

TEST_F(HotRestartingChildTest, ParentDrainedCallbacksAreCalled) {
  auto test_listener_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  auto test_listener_addr2 = *Network::Utility::resolveUrl("udp://127.0.0.1:1235");
  testing::MockFunction<void()> callback1;
  testing::MockFunction<void()> callback2;
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr,
                                                       callback1.AsStdFunction());
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr2,
                                                       callback2.AsStdFunction());
  EXPECT_CALL(callback1, Call());
  EXPECT_CALL(callback2, Call());
  fake_parent_->expectParentTerminateMessages();
  hot_restarting_child_->sendParentTerminateRequest();
}

TEST_F(HotRestartingChildTest, ParentDrainedCallbacksAreCalledImmediatelyWhenAlreadyDrained) {
  auto test_listener_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  auto test_listener_addr2 = *Network::Utility::resolveUrl("udp://127.0.0.1:1235");
  testing::MockFunction<void()> callback1;
  testing::MockFunction<void()> callback2;
  fake_parent_->expectParentTerminateMessages();
  hot_restarting_child_->sendParentTerminateRequest();
  EXPECT_CALL(callback1, Call());
  EXPECT_CALL(callback2, Call());
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr,
                                                       callback1.AsStdFunction());
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr2,
                                                       callback2.AsStdFunction());
}

TEST_F(HotRestartingChildTest, ParentStopAcceptingLatchesOnDrainRequest) {
  // With a parent (restart_epoch != 0), the child has not yet asked it to stop accepting.
  EXPECT_FALSE(hot_restarting_child_->parentStopAcceptingRequested());
  // drainParentListeners() sends the (fire-and-forget) drain-listeners request; expect one send.
  fake_parent_->expectParentTerminateMessages();
  hot_restarting_child_->drainParentListeners();
  EXPECT_TRUE(hot_restarting_child_->parentStopAcceptingRequested());
}

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
  auto test_listener_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  auto test_remote_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:4321");
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

// The hot restart deadlock: the parent forwards UDP packets to the child from its main thread
// and blocks when the child's socket is full; the child's main thread waits for the parent's
// reply (here: to a stats request) without reading forwarded packets. Each main thread then waits
// on the other forever. This models the parent's side of the dependency in the recvmsg mock: the
// stats reply is only available on the main socket once the forwarded packet has been read from
// the udp forwarding socket. Without the fix, the child never reads it while waiting.
class HotRestartingChildWaitingForParentTest : public HotRestartingChildTest {
public:
  void SetUp() override {
    HotRestartingChildTest::SetUp();
    helper_ = std::make_unique<HotRestartUdpForwardingTestHelper>(*hot_restarting_child_);
    HotRestartUdpForwardingTestHelper(*hot_restarting_child_)
        .registerUdpForwardingListener(
            test_listener_addr_,
            std::dynamic_pointer_cast<Network::UdpListenerConfig>(mock_udp_listener_config_));
    envoy::HotRestartMessage forwarded;
    auto* packet = forwarded.mutable_request()->mutable_forwarded_udp_packet();
    packet->set_local_addr(Network::Utility::urlFromDatagramAddress(*test_listener_addr_));
    packet->set_peer_addr(Network::Utility::urlFromDatagramAddress(*test_remote_addr_));
    packet->set_worker_index(worker_index_);
    packet->set_payload(udp_contents_);
    packet->set_receive_time_epoch_microseconds(packet_timestamp_);
    forwarded_wire_ = FakeHotRestartingParent::serialize(forwarded);
    envoy::HotRestartMessage stats_reply;
    (*stats_reply.mutable_reply()->mutable_stats()->mutable_gauges())["parent.gauge"] = 7;
    stats_reply_wire_ = FakeHotRestartingParent::serialize(stats_reply);
  }
  void expectPacketDelivered() {
    EXPECT_CALL(*mock_udp_listener_config_,
                listenerWorkerRouter(WhenDynamicCastTo<const Network::Address::Ipv4Instance&>(
                    Eq(dynamic_cast<const Network::Address::Ipv4Instance&>(*test_listener_addr_)))))
        .WillOnce(ReturnRef(mock_worker_router_));
    EXPECT_CALL(mock_worker_router_,
                deliver(worker_index_, IsUdpWith(test_listener_addr_, test_remote_addr_,
                                                 udp_contents_, packet_timestamp_)));
  }
  // recvmsg: the forwarded packet is waiting on the udp forwarding socket; the stats reply becomes
  // readable on the main socket only after the forwarded packet has been read.
  void parentRepliesOnlyAfterForwardIsRead() {
    EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _)).WillRepeatedly([this](int fd, msghdr* msg, int) {
      if (fd == helper_->udpForwardingSocketFd()) {
        if (forwarded_wire_.empty()) {
          return FakeHotRestartingParent::wouldBlock();
        }
        forward_read_ = true;
        return FakeHotRestartingParent::deliver(forwarded_wire_, msg);
      }
      EXPECT_EQ(fd, helper_->mainSocketFd());
      if (!forward_read_ || stats_reply_wire_.empty()) {
        main_socket_polls_++;
        return FakeHotRestartingParent::wouldBlock();
      }
      return FakeHotRestartingParent::deliver(stats_reply_wire_, msg);
    });
  }
  void parentNeverReplies() {
    EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _)).WillRepeatedly([this](int, msghdr*, int) {
      main_socket_polls_++;
      return FakeHotRestartingParent::wouldBlock();
    });
  }

  std::unique_ptr<HotRestartUdpForwardingTestHelper> helper_;
  std::shared_ptr<Network::MockUdpListenerConfig> mock_udp_listener_config_ =
      std::make_shared<Network::MockUdpListenerConfig>();
  Network::MockUdpListenerWorkerRouter mock_worker_router_;
  Network::Address::InstanceConstSharedPtr test_listener_addr_ =
      *Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  Network::Address::InstanceConstSharedPtr test_remote_addr_ =
      *Network::Utility::resolveUrl("udp://127.0.0.1:4321");
  const uint32_t worker_index_ = 3;
  const uint64_t packet_timestamp_ = 987654321;
  const std::string udp_contents_ = "beep boop";
  std::string forwarded_wire_;
  std::string stats_reply_wire_;
  bool forward_read_ = false;
  uint64_t main_socket_polls_ = 0;
};

TEST_F(HotRestartingChildWaitingForParentTest, ServicesUdpForwardingWhileWaitingForParentStats) {
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
    EXPECT_EQ(FakeHotRestartingParent::requestCase(msg), envoy::HotRestartMessage::Request::kStats);
    return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
  });
  parentRepliesOnlyAfterForwardIsRead();
  expectPacketDelivered();

  std::unique_ptr<envoy::HotRestartMessage> reply = hot_restarting_child_->getParentStats();
  ASSERT_NE(reply, nullptr);
  EXPECT_EQ(reply->reply().stats().gauges().at("parent.gauge"), 7);
  EXPECT_TRUE(forward_read_);
  // The reply was not available on the first read of the main socket.
  EXPECT_GE(main_socket_polls_, 1);
  EXPECT_FALSE(helper_->parentUnresponsive());
}

TEST_F(HotRestartingChildWaitingForParentTest,
       ServicesUdpForwardingWhileWaitingForParentListenSocket) {
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
    EXPECT_EQ(FakeHotRestartingParent::requestCase(msg),
              envoy::HotRestartMessage::Request::kPassListenSocket);
    return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
  });
  // The (wrong-typed) stats reply stands in for the parent's reply here: what matters is that the
  // wait reads the forwarded packet before the reply arrives and then returns.
  parentRepliesOnlyAfterForwardIsRead();
  expectPacketDelivered();

  EXPECT_EQ(hot_restarting_child_->duplicateParentListenSocket("udp://127.0.0.1:5678", 0, ""), -1);
  EXPECT_TRUE(forward_read_);
  EXPECT_FALSE(helper_->parentUnresponsive());
}

TEST_F(HotRestartingChildWaitingForParentTest, WritesParentOffAfterReplyTimeout) {
  helper_->setParentReplyTimeout(std::chrono::milliseconds(0));
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
    return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
  });
  parentNeverReplies();
  testing::MockFunction<void()> drained_callback;
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr_,
                                                       drained_callback.AsStdFunction());

  EXPECT_LOG_CONTAINS("error", "hot restart parent is unresponsive (no reply within 0ms)",
                      EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr));
  EXPECT_TRUE(helper_->parentUnresponsive());
  // Unresponsive is not gone: the parent may still be serving, so the drains stay pending until
  // the parent is asked to terminate.
  EXPECT_FALSE(helper_->parentTerminated());

  // No further request waits on the parent (no sendmsg/recvmsg expectations remain to be met).
  EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr);
  EXPECT_EQ(hot_restarting_child_->duplicateParentListenSocket("udp://127.0.0.1:5678", 0, ""), -1);
  EXPECT_EQ(hot_restarting_child_->sendParentAdminShutdownRequest(), std::nullopt);

  // The terminate request is still sent, and completes the drains.
  EXPECT_CALL(drained_callback, Call());
  fake_parent_->expectParentTerminateMessages();
  hot_restarting_child_->sendParentTerminateRequest();
  EXPECT_TRUE(helper_->parentTerminated());
}

// A listen socket request has its own, shorter deadline: a listener add must not stall the main
// thread for as long as a stats merge may. After it the parent is written off like after any
// other timeout, and the caller binds its own socket (paused, since the parent never answered).
TEST_F(HotRestartingChildWaitingForParentTest, ListenSocketRequestGivesUpAfterItsOwnDeadline) {
  // The general deadline stays long; only the listen socket one is exhausted at once.
  helper_->setParentReplyTimeout(std::chrono::seconds(30));
  helper_->setParentListenSocketReplyTimeout(std::chrono::milliseconds(0));
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
    EXPECT_EQ(FakeHotRestartingParent::requestCase(msg),
              envoy::HotRestartMessage::Request::kPassListenSocket);
    return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
  });
  parentNeverReplies();
  EXPECT_FALSE(hot_restarting_child_->parentUnresponsive());
  EXPECT_LOG_CONTAINS(
      "error", "hot restart parent is unresponsive (no reply within 0ms)",
      EXPECT_EQ(hot_restarting_child_->duplicateParentListenSocket("udp://127.0.0.1:5678", 0, ""),
                -1));
  EXPECT_TRUE(hot_restarting_child_->parentUnresponsive());
  EXPECT_FALSE(helper_->parentTerminated());
  // Nothing else waits on the parent afterwards (no further sendmsg/recvmsg expectations).
  EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr);
}

TEST_F(HotRestartingChildWaitingForParentTest, CompletesDrainsWhenProbeFindsParentGone) {
  helper_->setParentProbeInterval(std::chrono::milliseconds(0));
  {
    testing::InSequence s;
    // The stats request goes out; the liveness probe is refused: the parent's socket is gone.
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
      EXPECT_EQ(FakeHotRestartingParent::requestCase(msg),
                envoy::HotRestartMessage::Request::kStats);
      return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
    });
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
      EXPECT_EQ(FakeHotRestartingParent::requestCase(msg),
                envoy::HotRestartMessage::Request::kTestConnection);
      return Api::SysCallSizeResult{-1, ECONNREFUSED};
    });
  }
  parentNeverReplies();
  testing::MockFunction<void()> drained_callback;
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr_,
                                                       drained_callback.AsStdFunction());

  EXPECT_CALL(drained_callback, Call());
  EXPECT_LOG_CONTAINS("error", "hot restart parent is gone",
                      EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr));
  EXPECT_TRUE(helper_->parentUnresponsive());
  EXPECT_TRUE(helper_->parentTerminated());

  // Nothing more is sent to a parent that is gone, terminate included.
  hot_restarting_child_->sendParentTerminateRequest();
  EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr);
}

TEST_F(HotRestartingChildWaitingForParentTest, WritesParentOffWhenProbeTimesOut) {
  helper_->setParentProbeInterval(std::chrono::milliseconds(0));
  {
    testing::InSequence s;
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr* msg, int) {
      return Api::SysCallSizeResult{static_cast<ssize_t>(msg->msg_iov[0].iov_len), 0};
    });
    // The probe's blocking send times out: the parent is not draining its socket.
    EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr*, int) {
      return Api::SysCallSizeResult{-1, EAGAIN};
    });
  }
  parentNeverReplies();
  testing::MockFunction<void()> drained_callback;
  hot_restarting_child_->registerParentDrainedCallback(test_listener_addr_,
                                                       drained_callback.AsStdFunction());

  EXPECT_LOG_CONTAINS("error", "hot restart parent is unresponsive (its socket is full",
                      EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr));
  EXPECT_TRUE(helper_->parentUnresponsive());
  EXPECT_FALSE(helper_->parentTerminated());
}

TEST_F(HotRestartingChildWaitingForParentTest, WritesParentOffWhenRequestSendTimesOut) {
  EXPECT_CALL(os_sys_calls_, sendmsg(_, _, _)).WillOnce([](int, const msghdr*, int) {
    return Api::SysCallSizeResult{-1, EAGAIN};
  });
  EXPECT_LOG_CONTAINS("error", "the stats request could not be sent",
                      EXPECT_EQ(hot_restarting_child_->getParentStats(), nullptr));
  EXPECT_TRUE(helper_->parentUnresponsive());
  EXPECT_FALSE(helper_->parentTerminated());
}

// A parent that does not send the listener address is matched on the packet's destination,
// falling back to the any-address listener on the port.
TEST_F(HotRestartingChildTest, ForwardsPacketToAnyAddressListenerByDestination) {
  uint32_t worker_index = 1;
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  auto mock_udp_listener_config = std::make_shared<Network::MockUdpListenerConfig>();
  auto test_listener_addr = *Network::Utility::resolveUrl("udp://0.0.0.0:1234");
  auto test_local_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:1234");
  auto test_remote_addr = *Network::Utility::resolveUrl("udp://127.0.0.1:4321");
  HotRestartUdpForwardingTestHelper(*hot_restarting_child_)
      .registerUdpForwardingListener(
          test_listener_addr,
          std::dynamic_pointer_cast<Network::UdpListenerConfig>(mock_udp_listener_config));
  packet->set_local_addr(Network::Utility::urlFromDatagramAddress(*test_local_addr));
  packet->set_peer_addr(Network::Utility::urlFromDatagramAddress(*test_remote_addr));
  packet->set_worker_index(worker_index);
  packet->set_payload("x");
  Network::MockUdpListenerWorkerRouter mock_worker_router;
  EXPECT_CALL(*mock_udp_listener_config,
              listenerWorkerRouter(WhenDynamicCastTo<const Network::Address::Ipv4Instance&>(
                  Eq(dynamic_cast<const Network::Address::Ipv4Instance&>(*test_listener_addr)))))
      .WillOnce(ReturnRef(mock_worker_router));
  EXPECT_CALL(mock_worker_router, deliver(worker_index, IsUdpWith(test_local_addr, test_remote_addr,
                                                                  "x", uint64_t(0))));
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

// Listeners bound to the same address in different network namespaces are distinct, and a
// forwarded packet is delivered to the listener the parent names, not to whichever registered
// first and not to the one matching the packet's (transparent) destination.
TEST_F(HotRestartingChildTest, ForwardsPacketToListenerInNamedNetworkNamespace) {
  uint32_t worker_index = 2;
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  auto listener_config_a = std::make_shared<Network::MockUdpListenerConfig>();
  auto listener_config_b = std::make_shared<Network::MockUdpListenerConfig>();
  auto listener_addr_a =
      Network::Utility::resolveUrl("udp://0.0.0.0:1234").value()->withNetworkNamespace("/ns/a");
  auto listener_addr_b =
      Network::Utility::resolveUrl("udp://0.0.0.0:1234").value()->withNetworkNamespace("/ns/b");
  // Destination of a transparent socket: not the bind address of either listener.
  auto test_local_addr = *Network::Utility::resolveUrl("udp://10.0.0.5:1234");
  auto test_remote_addr = *Network::Utility::resolveUrl("udp://10.0.0.9:4321");
  HotRestartUdpForwardingTestHelper helper(*hot_restarting_child_);
  helper.registerUdpForwardingListener(
      listener_addr_a, std::dynamic_pointer_cast<Network::UdpListenerConfig>(listener_config_a));
  helper.registerUdpForwardingListener(
      listener_addr_b, std::dynamic_pointer_cast<Network::UdpListenerConfig>(listener_config_b));
  packet->set_local_addr(Network::Utility::urlFromDatagramAddress(*test_local_addr));
  packet->set_peer_addr(Network::Utility::urlFromDatagramAddress(*test_remote_addr));
  packet->set_worker_index(worker_index);
  packet->set_payload("y");
  packet->set_listener_addr("udp://0.0.0.0:1234");
  packet->set_network_namespace("/ns/b");
  Network::MockUdpListenerWorkerRouter mock_worker_router;
  EXPECT_CALL(*listener_config_a, listenerWorkerRouter(_)).Times(0);
  EXPECT_CALL(*listener_config_b,
              listenerWorkerRouter(WhenDynamicCastTo<const Network::Address::Ipv4Instance&>(
                  Eq(dynamic_cast<const Network::Address::Ipv4Instance&>(*listener_addr_b)))))
      .WillOnce(ReturnRef(mock_worker_router));
  EXPECT_CALL(mock_worker_router, deliver(worker_index, IsUdpWith(test_local_addr, test_remote_addr,
                                                                  "y", uint64_t(0))));
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

// A listener is not matched across network namespaces, nor by a namespace-less lookup.
TEST_F(HotRestartingChildTest, DoesNotForwardAcrossNetworkNamespaces) {
  envoy::HotRestartMessage msg;
  auto* packet = msg.mutable_request()->mutable_forwarded_udp_packet();
  auto listener_config = std::make_shared<Network::MockUdpListenerConfig>();
  auto listener_addr =
      Network::Utility::resolveUrl("udp://0.0.0.0:1234").value()->withNetworkNamespace("/ns/a");
  HotRestartUdpForwardingTestHelper(*hot_restarting_child_)
      .registerUdpForwardingListener(
          listener_addr, std::dynamic_pointer_cast<Network::UdpListenerConfig>(listener_config));
  packet->set_local_addr("udp://10.0.0.5:1234");
  packet->set_peer_addr("udp://10.0.0.9:4321");
  packet->set_payload("z");
  EXPECT_CALL(*listener_config, listenerWorkerRouter(_)).Times(0);

  // Another namespace.
  packet->set_listener_addr("udp://0.0.0.0:1234");
  packet->set_network_namespace("/ns/c");
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));

  // No namespace (older parent): the destination lookup must not reach a namespaced listener.
  packet->clear_listener_addr();
  packet->clear_network_namespace();
  EXPECT_LOG_NOT_CONTAINS("error", "", fake_parent_->sendUdpForwardingMessage(msg));
}

} // namespace
} // namespace Server
} // namespace Envoy
