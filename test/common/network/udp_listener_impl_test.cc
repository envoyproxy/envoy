#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/os_sys_calls.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"

#include "test/common/network/udp_listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mock_parent_drained_callback_registrar.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {

// UdpGro is only supported on Linux versions >= 5.0. Also, the
// underlying platform only performs the payload concatenation when
// packets are sent from a network namespace different to that of
// the client. Currently, the testing framework does not support
// this behavior.
// This helper allows to intercept syscalls and
// toggle the behavior as per individual test requirements.
class OverrideOsSysCallsImpl : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
  MOCK_METHOD(bool, supportsMmsg, (), (const));
};

class UdpListenerImplTest : public UdpListenerImplTestBase {
public:
  void setup(bool prefer_gro = false) {
    UdpListenerImplTestBase::setup();
    ON_CALL(override_syscall_, supportsUdpGro()).WillByDefault(Return(false));
    // Return the real version by default.
    ON_CALL(override_syscall_, supportsMmsg())
        .WillByDefault(Return(os_calls.latched().supportsMmsg()));
    ON_CALL(listener_callbacks_, numPacketsExpectedPerEventLoop())
        .WillByDefault(Return(MAX_NUM_PACKETS_PER_EVENT_LOOP));
    ON_CALL(listener_callbacks_, udpSaveCmsgConfig())
        .WillByDefault(ReturnRef(udp_save_cmsg_config_));

    // Set listening socket options.
    server_socket_->addOptions(SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(SocketOptionFactory::buildRxQueueOverFlowOptions());
    if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
      server_socket_->addOptions(SocketOptionFactory::buildUdpGroOptions());
    }
    std::unique_ptr<Network::Socket::Options> options =
        std::make_unique<Network::Socket::Options>();
    options->push_back(std::make_shared<Network::SocketOptionImpl>(
        envoy::config::core::v3::SocketOption::STATE_BOUND,
        ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_RCVBUF), 4 * 1024 * 1024));
    server_socket_->addOptions(std::move(options));
    ASSERT_TRUE(Network::Socket::applyOptions(server_socket_->options(), *server_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));
    envoy::config::core::v3::UdpSocketConfig config;
    if (prefer_gro) {
      config.mutable_prefer_gro()->set_value(prefer_gro);
    }
    listener_ =
        std::make_unique<UdpListenerImpl>(dispatcherImpl(), server_socket_, listener_callbacks_,
                                          dispatcherImpl().timeSource(), config);
    udp_packet_writer_ = std::make_unique<Network::UdpDefaultWriter>(server_socket_->ioHandle());
    int get_recvbuf_size = 0;
    socklen_t int_size = static_cast<socklen_t>(sizeof(get_recvbuf_size));
    const Api::SysCallIntResult result2 =
        server_socket_->getSocketOption(SOL_SOCKET, SO_RCVBUF, &get_recvbuf_size, &int_size);
    EXPECT_EQ(0, result2.return_value_);
    // Kernel increases the buffer size to allow bookkeeping overhead.
    if (get_recvbuf_size < 4 * 1024 * 1024) {
      recvbuf_large_enough_ = false;
    }

    ON_CALL(listener_callbacks_, udpPacketWriter()).WillByDefault(ReturnRef(*udp_packet_writer_));
  }

  NiceMock<OverrideOsSysCallsImpl> override_syscall_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&override_syscall_};
  bool recvbuf_large_enough_{true};
  const IoHandle::UdpSaveCmsgConfig udp_save_cmsg_config_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UdpListenerImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Tests UDP listener for actual destination and data.
 */
TEST_P(UdpListenerImplTest, UseActualDstUdp) {
  setup();

  // We send 2 packets
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(
            data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);
        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(
            data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);
        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test a large datagram that gets dropped using recvmsg or recvmmsg if supported.
TEST_P(UdpListenerImplTest, LargeDatagramRecvmmsg) {
  setup();

  // This will get dropped.
  const std::string first(4096, 'a');
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);
  // This will get dropped.
  const std::string third(4096, 'b');
  client_.write(third, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onDatagramsDropped(_)).Times(AtLeast(1));
  EXPECT_CALL(listener_callbacks_, onData(_)).WillOnce(Invoke([&](const UdpRecvData& data) -> void {
    validateRecvCallbackParams(
        data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);
    EXPECT_EQ(data.buffer_->toString(), second);

    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(2, listener_->packetsDropped());
}

TEST_P(UdpListenerImplTest, LimitNumberOfReadsPerLoop) {
  setup();
  const uint64_t num_packets_per_read =
      Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u;

  size_t num_packets_expected_per_loop{32u};
  // These packets should be read in more than 3 loops.
  const std::string payload1(10, 'a');
  for (uint64_t i = 0; i < 2 * num_packets_expected_per_loop; ++i) {
    client_.write(payload1, *send_to_addr_);
  }
  const std::string last_piece("bbb");
  client_.write(last_piece, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady()).Times(testing::AtLeast(3u));
  EXPECT_CALL(listener_callbacks_, numPacketsExpectedPerEventLoop())
      .WillRepeatedly(Return(num_packets_expected_per_loop));
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, num_packets_per_read);
        if (last_piece == data.buffer_->toString()) {
          dispatcher_->exit();
        }
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  num_packets_received_by_listener_ = 0u;
  num_packets_expected_per_loop = 0u;
  std::string payload2(10, 'c');
  // This packet should be read.
  client_.write(payload2, *send_to_addr_);
  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, numPacketsExpectedPerEventLoop())
      .WillRepeatedly(Return(num_packets_expected_per_loop));
  EXPECT_CALL(listener_callbacks_, onData(_)).WillOnce(Invoke([&](const UdpRecvData& data) -> void {
    validateRecvCallbackParams(data, num_packets_per_read);
    EXPECT_EQ(payload2, data.buffer_->toString());
    dispatcher_->exit();
  }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  if (!recvbuf_large_enough_) {
    // If SO_RCVBUF failed to enlarge receive buffer to 4MB, the rest of test will likely to fail
    // because packets may be easily dropped. Skip the rest of the test.
    return;
  }
  num_packets_received_by_listener_ = 0u;
  // Though the mocked callback wants to read more, only 6000 reads maximum are allowed.
  num_packets_expected_per_loop = MAX_NUM_PACKETS_PER_EVENT_LOOP + 1u;
  std::string payload3(10, 'd');
  for (uint64_t i = 0; i < num_packets_expected_per_loop; ++i) {
    client_.write(payload3, *send_to_addr_);
  }
  std::string really_last_piece("eee");
  client_.write(really_last_piece, *send_to_addr_);
  EXPECT_CALL(listener_callbacks_, onReadReady()).Times(testing::AtLeast(2u));
  EXPECT_CALL(listener_callbacks_, numPacketsExpectedPerEventLoop())
      .WillRepeatedly(Return(num_packets_expected_per_loop));
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, num_packets_per_read);
        if (really_last_piece == data.buffer_->toString()) {
          dispatcher_->exit();
        }
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

#ifdef UDP_GRO
TEST_P(UdpListenerImplTest, GroLargeDatagramRecvmsg) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.udp_socket_apply_aggregated_read_limit")) {
    return;
  }
  setup(true);

  ON_CALL(override_syscall_, supportsUdpGro()).WillByDefault(Return(true));
  client_.write(std::string(32768, 'a'), *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onDatagramsDropped(_)).Times(AtLeast(1));
  EXPECT_CALL(listener_callbacks_, onData(_)).WillOnce(Invoke([&](const UdpRecvData& data) -> void {
    validateRecvCallbackParams(data, 1);
    EXPECT_EQ(data.buffer_->toString(), second);

    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(1, listener_->packetsDropped());
}
#endif

/**
 * Tests UDP listener for read and write callbacks with actual data.
 */
TEST_P(UdpListenerImplTest, UdpEcho) {
  setup();

  // We send 17 packets and expect it to echo.
  absl::FixedArray<std::string> client_data({"first", "second", "third", "forth", "fifth", "sixth",
                                             "seventh", "eighth", "ninth", "tenth", "eleventh",
                                             "twelveth", "thirteenth", "fourteenth", "fifteenth",
                                             "sixteenth", "seventeenth"});
  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // For unit test purposes, we assume that the data was received in order.
  Address::InstanceConstSharedPtr test_peer_address;

  std::vector<std::string> server_received_data;

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(
            data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);

        test_peer_address = data.addresses_.peer_;

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }))
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(
            data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);

        server_received_data.push_back(data_str);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
    ASSERT_NE(test_peer_address, nullptr);

    for (const auto& data : server_received_data) {
      const std::string::size_type data_size = data.length() + 1;
      uint64_t total_sent = 0;
      const void* void_data = static_cast<const void*>(data.c_str() + total_sent);
      Buffer::RawSlice slice{const_cast<void*>(void_data), data_size - total_sent};

      Api::IoCallUint64Result send_rc = Api::ioCallUint64ResultNoError();
      do {
        send_rc = Network::Utility::writeToSocket(const_cast<Socket*>(&socket)->ioHandle(), &slice,
                                                  1, nullptr, *test_peer_address);

        if (send_rc.ok()) {
          total_sent += send_rc.return_value_;
          if (total_sent >= data_size) {
            break;
          }
        } else if (send_rc.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          break;
        }
      } while (((send_rc.return_value_ == 0) &&
                (send_rc.err_->getErrorCode() == Api::IoError::IoErrorCode::Again)) ||
               (total_sent < data_size));

      EXPECT_EQ(total_sent, data_size);
    }

    server_received_data.clear();
    dispatcher_->exit();
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener's `enable` and `disable` APIs.
 */
TEST_P(UdpListenerImplTest, UdpListenerEnableDisable) {
  setup();

  auto const* server_ip = server_socket_->connectionInfoProvider().localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // We first disable the listener and then send two packets.
  // - With the listener disabled, we expect that none of the callbacks will be
  // called.
  // - When the listener is enabled back, we expect the callbacks to be called
  listener_->disable();
  const std::string first("first");
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady()).Times(0);
  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).Times(0);

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  listener_->enable();

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onData(_))
      .Times(2)
      .WillOnce(Return())
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(
            data, Api::OsSysCallsSingleton::get().supportsMmsg() ? NUM_DATAGRAMS_PER_RECEIVE : 1u);

        EXPECT_EQ(data.buffer_->toString(), second);

        dispatcher_->exit();
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_))
      .WillRepeatedly(Invoke([&](const Socket& socket) {
        EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

class HotRestartedUdpListenerImplTest : public UdpListenerImplTest {
public:
  void SetUp() override {
#ifdef WIN32
    GTEST_SKIP() << "Hot restart is not supported on Windows.";
#endif
  }
  void setup() {
    io_handle_ = &useHotRestartSocket(registrar_);
    // File event should be created listening to no events (i.e. disabled).
    EXPECT_CALL(*io_handle_, createFileEvent_(_, _, _, 0));
    // Parent drained callback should be registered when the listener is created.
    // We capture the callback so we can simulate "drain complete".
    EXPECT_CALL(registrar_, registerParentDrainedCallback(_, _))
        .WillOnce(
            [this](const Address::InstanceConstSharedPtr&, absl::AnyInvocable<void()> callback) {
              parent_drained_callback_ = std::move(callback);
            });
    UdpListenerImplTest::setup();
    testing::Mock::VerifyAndClearExpectations(&registrar_);
  }

protected:
  MockParentDrainedCallbackRegistrar registrar_;
  MockIoHandle* io_handle_;
  absl::AnyInvocable<void()> parent_drained_callback_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HotRestartedUdpListenerImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * During hot restart, while the parent instance is draining, a quic udp
 * listener (created with a parent_drained_callback_registrar) should not
 * be reading packets, regardless of enable/disable calls.
 * It should begin reading packets after drain completes.
 */
TEST_P(HotRestartedUdpListenerImplTest, EnableAndDisableDuringParentDrainShouldDoNothing) {
  setup();
  // Enabling and disabling listener should *not* trigger any
  // event actions on the io_handle, because of listener being paused
  // while draining.
  EXPECT_CALL(*io_handle_, enableFileEvents(_)).Times(0);
  listener_->disable();
  listener_->enable();
  testing::Mock::VerifyAndClearExpectations(io_handle_);
  // Ending parent drain should cause io_handle to go into reading mode.
  EXPECT_CALL(*io_handle_,
              enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write));
  EXPECT_CALL(*io_handle_, activateFileEvents(Event::FileReadyType::Read));
  std::move(parent_drained_callback_)();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  testing::Mock::VerifyAndClearExpectations(io_handle_);
  // Enabling and disabling once unpaused should update io_handle.
  EXPECT_CALL(*io_handle_, enableFileEvents(0));
  listener_->disable();
  testing::Mock::VerifyAndClearExpectations(io_handle_);
  EXPECT_CALL(*io_handle_,
              enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write));
  listener_->enable();
  testing::Mock::VerifyAndClearExpectations(io_handle_);
}

/**
 * Mostly the same as EnableAndDisableDuringParentDrainShouldDoNothing, but in disabled state when
 * drain ends.
 */
TEST_P(HotRestartedUdpListenerImplTest, EndingParentDrainedWhileDisabledShouldNotStartReading) {
  setup();
  // Enabling and disabling listener should *not* trigger any
  // event actions on the io_handle, because of listener being paused
  // while draining.
  EXPECT_CALL(*io_handle_, enableFileEvents(_)).Times(0);
  listener_->enable();
  listener_->disable();
  testing::Mock::VerifyAndClearExpectations(io_handle_);
  // Ending drain should not trigger any event changes because the last state
  // of the listener was disabled.
  std::move(parent_drained_callback_)();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  testing::Mock::VerifyAndClearExpectations(io_handle_);
  // Enabling after unpaused should set io_handle to reading/writing.
  EXPECT_CALL(*io_handle_,
              enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write));
  listener_->enable();
  testing::Mock::VerifyAndClearExpectations(io_handle_);
}

TEST_P(HotRestartedUdpListenerImplTest,
       ParentDrainedCallbackAfterListenerDestroyedShouldDoNothing) {
  setup();
  EXPECT_CALL(*io_handle_, enableFileEvents(_)).Times(0);
  listener_ = nullptr;
  // Signaling end-of-drain after the listener was destroyed should do nothing.
  std::move(parent_drained_callback_)();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // At this point io_handle should be an invalid reference.
}

/**
 * Tests UDP listener's error callback.
 */
TEST_P(UdpListenerImplTest, UdpListenerRecvMsgError) {
  setup();

  auto const* server_ip = server_socket_->connectionInfoProvider().localAddress()->ip();
  ASSERT_NE(server_ip, nullptr);

  // When the `receive` system call returns an error, we expect the `onReceiveError`
  // callback called with `SyscallError` parameter.
  const std::string first("first");
  client_.write(first, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onData(_)).Times(0);

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
  }));

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onReceiveError(_))
      .WillOnce(Invoke([&](Api::IoError::IoErrorCode err) -> void {
        ASSERT_EQ(Api::IoError::IoErrorCode::NoSupport, err);
        dispatcher_->exit();
      }));
  // Inject mocked OsSysCalls implementation to mock a read failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsMmsg()).Times((1u));
  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOT_SUP}));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

/**
 * Tests UDP listener for sending datagrams to destination.
 *  1. Setup a udp listener and client socket
 *  2. Send the data from the udp listener to the client socket and validate the contents and source
 * address.
 */
TEST_P(UdpListenerImplTest, SendData) {
  setup();

  EXPECT_FALSE(udp_packet_writer_->isBatchMode());
  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);

  Address::InstanceConstSharedPtr send_from_addr = getNonDefaultSourceAddress();

  UdpSendData send_data{send_from_addr->ip(), *client_.localAddress(), *buffer};

  auto send_result = listener_->send(send_data);

  EXPECT_TRUE(send_result.ok()) << "send() failed : " << send_result.err_->getErrorDetails();

  const uint64_t bytes_to_read = payload.length();
  UdpRecvData data;
  client_.recv(data);
  EXPECT_EQ(bytes_to_read, data.buffer_->length());
  EXPECT_EQ(send_from_addr->asString(), data.addresses_.peer_->asString());
  EXPECT_EQ(data.buffer_->toString(), payload);

  // Verify External Flush is a No-op
  auto flush_result = udp_packet_writer_->flush();
  EXPECT_TRUE(flush_result.ok());
  EXPECT_EQ(0, flush_result.return_value_);
}

/**
 * The send fails because the server_socket is created with bind=false.
 */
TEST_P(UdpListenerImplTest, SendDataError) {
  setup();

  const std::string payload("hello world");
  Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
  buffer->add(payload);
  // send data to itself
  UdpSendData send_data{send_to_addr_->ip(),
                        *server_socket_->connectionInfoProvider().localAddress(), *buffer};

  // Inject mocked OsSysCalls implementation to mock a write failure.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));
  auto send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());
  // Verify the writer is set to blocked
  EXPECT_TRUE(udp_packet_writer_->isWriteBlocked());

  // Reset write_blocked status
  udp_packet_writer_->setWritable();
  EXPECT_FALSE(udp_packet_writer_->isWriteBlocked());

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_NOT_SUP}));
  send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::NoSupport);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_INVAL}));
  send_result = listener_->send(send_data);
  EXPECT_FALSE(send_result.ok());
  EXPECT_EQ(send_result.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
  // Failed write shouldn't drain the data.
  EXPECT_EQ(payload.length(), buffer->length());
}

/**
 * Test that multiple stacked packets of the same size are properly segmented
 * when UDP GRO is enabled on the platform.
 */
#ifdef UDP_GRO
TEST_P(UdpListenerImplTest, UdpGroBasic) {
  setup(true);

  // We send 4 packets (3 of equal length and 1 as a trail), which are concatenated together by
  // kernel supporting udp gro. Verify the concatenated packet is transformed back into individual
  // packets
  absl::FixedArray<std::string> client_data({"Equal!!!", "Length!!", "Messages", "trail"});

  for (const auto& i : client_data) {
    client_.write(i, *send_to_addr_);
  }

  // The concatenated payload received from kernel supporting udp_gro
  std::string stacked_message = absl::StrJoin(client_data, "");

  // Mock OsSysCalls to mimic kernel behavior for packet concatenation
  // based on udp_gro. supportsUdpGro should return true and recvmsg should
  // return the concatenated payload with the gso_size set appropriately.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, supportsMmsg).Times(0);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Invoke([&](os_fd_t, msghdr* msg, int) {
        // Set msg_name and msg_namelen
        if (client_.localAddress()->ip()->version() == Address::IpVersion::v4) {
          sockaddr_storage ss;
          auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
          memset(ipv4_addr, 0, sizeof(sockaddr_in));
          ipv4_addr->sin_family = AF_INET;
          ipv4_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
          ipv4_addr->sin_port = client_.localAddress()->ip()->port();
          msg->msg_namelen = sizeof(sockaddr_in);
          *reinterpret_cast<sockaddr_in*>(msg->msg_name) = *ipv4_addr;
        } else if (client_.localAddress()->ip()->version() == Address::IpVersion::v6) {
          sockaddr_storage ss;
          auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
          memset(ipv6_addr, 0, sizeof(sockaddr_in6));
          ipv6_addr->sin6_family = AF_INET6;
          ipv6_addr->sin6_addr = in6addr_loopback;
          ipv6_addr->sin6_port = client_.localAddress()->ip()->port();
          *reinterpret_cast<sockaddr_in6*>(msg->msg_name) = *ipv6_addr;
          msg->msg_namelen = sizeof(sockaddr_in6);
        }

        // Set msg_iovec
        EXPECT_EQ(msg->msg_iovlen, 1);
        memcpy(msg->msg_iov[0].iov_base, stacked_message.data(), stacked_message.length());
        if (Runtime::runtimeFeatureEnabled(
                "envoy.reloadable_features.udp_socket_apply_aggregated_read_limit")) {
          EXPECT_EQ(msg->msg_iov[0].iov_len, 64 * 1024);
        }
        msg->msg_iov[0].iov_len = stacked_message.length();

        // Set control headers
        memset(msg->msg_control, 0, msg->msg_controllen);
        cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
        if (send_to_addr_->ip()->version() == Address::IpVersion::v4) {
          cmsg->cmsg_level = IPPROTO_IP;
#ifndef IP_RECVDSTADDR
          cmsg->cmsg_type = IP_PKTINFO;
          cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
          reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg))->ipi_addr.s_addr =
              send_to_addr_->ip()->ipv4()->address();
#else
          cmsg.cmsg_type = IP_RECVDSTADDR;
          cmsg->cmsg_len = CMSG_LEN(sizeof(in_addr));
          *reinterpret_cast<in_addr*>(CMSG_DATA(cmsg)) = send_to_addr_->ip()->ipv4()->address();
#endif
        } else if (send_to_addr_->ip()->version() == Address::IpVersion::v6) {
          cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
          cmsg->cmsg_level = IPPROTO_IPV6;
          cmsg->cmsg_type = IPV6_PKTINFO;
          auto pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
          pktinfo->ipi6_ifindex = 0;
          *(reinterpret_cast<absl::uint128*>(pktinfo->ipi6_addr.s6_addr)) =
              send_to_addr_->ip()->ipv6()->address();
        }

        // Set gso_size
        cmsg = CMSG_NXTHDR(msg, cmsg);
        cmsg->cmsg_level = SOL_UDP;
        cmsg->cmsg_type = UDP_GRO;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
        const uint16_t gso_size = 8;
        *reinterpret_cast<uint16_t*>(CMSG_DATA(cmsg)) = gso_size;

#ifdef SO_RXQ_OVFL
        // Set SO_RXQ_OVFL
        cmsg = CMSG_NXTHDR(msg, cmsg);
        EXPECT_NE(cmsg, nullptr);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SO_RXQ_OVFL;
        cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
        const uint32_t overflow = 0;
        *reinterpret_cast<uint32_t*>(CMSG_DATA(cmsg)) = overflow;
#endif
        return Api::SysCallSizeResult{static_cast<long>(stacked_message.length()), 0};
      }))
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, EAGAIN}));

  EXPECT_CALL(listener_callbacks_, onReadReady()).WillOnce(Invoke([&]() { dispatcher_->exit(); }));
  EXPECT_CALL(listener_callbacks_, onData(_))
      .Times(4u)
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, client_data.size());

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(UdpListenerImplTest, GroLargeDatagramRecvmsgNoDrop) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.udp_socket_apply_aggregated_read_limit")) {
    return;
  }
  setup(true);

  ON_CALL(override_syscall_, supportsUdpGro()).WillByDefault(Return(true));
  const std::string first = std::string(32 * 1024, 'a');
  client_.write(first, *send_to_addr_);
  const std::string second("second");
  client_.write(second, *send_to_addr_);

  EXPECT_CALL(listener_callbacks_, onReadReady());
  EXPECT_CALL(listener_callbacks_, onDatagramsDropped(_)).Times(0);
  EXPECT_CALL(listener_callbacks_, onData(_))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, 1);
        EXPECT_EQ(data.buffer_->toString(), first);
      }))
      .WillOnce(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, 1);
        EXPECT_EQ(data.buffer_->toString(), second);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(0, listener_->packetsDropped());
}

// Tests that with GRO, listener are able to read 64kB worth of data if they are carried in packets
// of same size, regardless of MAX_NUM_PACKETS_PER_EVENT_LOOP or listener_callbacks_ provided limit.
// But once MAX_NUM_PACKETS_PER_EVENT_LOOP of packets are processed, read will stop.
TEST_P(UdpListenerImplTest, UdpGroReadLimit) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.udp_socket_apply_aggregated_read_limit")) {
    return;
  }
  setup(true);

  EXPECT_CALL(listener_callbacks_, numPacketsExpectedPerEventLoop()).WillRepeatedly(Return(32));
  // We send 128 packets of 1024kB. Verify the concatenated packet is transformed back into
  // individual packets and only 32 packets (64kB) are read in one batch.
  std::vector<std::string> client_data;
  std::string stacked_message;
  for (size_t i = 0; i < 128; i++) {
    // Interleave packets with 1024 bytes of 'a' and 1024 bytes of 'b'.
    std::string payload(1024, (i % 2 == 0 ? 'a' : 'b'));
    client_data.push_back(payload);
    // The concatenated payload received from kernel supporting udp_gro
    stacked_message = absl::StrCat(stacked_message, payload);
    // Actually send the packets to trigger I/O events on the listener.
    client_.write(payload, *send_to_addr_);
  }

  // Mock OsSysCalls to mimic kernel behavior for packet concatenation
  // based on udp_gro. supportsUdpGro should return true and recvmsg should
  // return the concatenated payload with the gso_size set appropriately.
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, supportsMmsg).Times(0);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, msghdr* msg, int) {
    // Set msg_name and msg_namelen
    if (client_.localAddress()->ip()->version() == Address::IpVersion::v4) {
      sockaddr_storage ss;
      auto ipv4_addr = reinterpret_cast<sockaddr_in*>(&ss);
      memset(ipv4_addr, 0, sizeof(sockaddr_in));
      ipv4_addr->sin_family = AF_INET;
      ipv4_addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      ipv4_addr->sin_port = client_.localAddress()->ip()->port();
      msg->msg_namelen = sizeof(sockaddr_in);
      *reinterpret_cast<sockaddr_in*>(msg->msg_name) = *ipv4_addr;
    } else if (client_.localAddress()->ip()->version() == Address::IpVersion::v6) {
      sockaddr_storage ss;
      auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
      memset(ipv6_addr, 0, sizeof(sockaddr_in6));
      ipv6_addr->sin6_family = AF_INET6;
      ipv6_addr->sin6_addr = in6addr_loopback;
      ipv6_addr->sin6_port = client_.localAddress()->ip()->port();
      *reinterpret_cast<sockaddr_in6*>(msg->msg_name) = *ipv6_addr;
      msg->msg_namelen = sizeof(sockaddr_in6);
    }

    // Set msg_iovec
    EXPECT_EQ(msg->msg_iovlen, 1);
    EXPECT_EQ(msg->msg_iov[0].iov_len, 64 * 1024);
    memcpy(msg->msg_iov[0].iov_base, stacked_message.data(), 64 * 1024);
    msg->msg_iov[0].iov_len = 64 * 1024;

    // Set control headers
    memset(msg->msg_control, 0, msg->msg_controllen);
    cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
    if (send_to_addr_->ip()->version() == Address::IpVersion::v4) {
      cmsg->cmsg_level = IPPROTO_IP;
#ifndef IP_RECVDSTADDR
      cmsg->cmsg_type = IP_PKTINFO;
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
      reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg))->ipi_addr.s_addr =
          send_to_addr_->ip()->ipv4()->address();
#else
      cmsg.cmsg_type = IP_RECVDSTADDR;
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_addr));
      *reinterpret_cast<in_addr*>(CMSG_DATA(cmsg)) = send_to_addr_->ip()->ipv4()->address();
#endif
    } else if (send_to_addr_->ip()->version() == Address::IpVersion::v6) {
      cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
      cmsg->cmsg_level = IPPROTO_IPV6;
      cmsg->cmsg_type = IPV6_PKTINFO;
      auto pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
      pktinfo->ipi6_ifindex = 0;
      *(reinterpret_cast<absl::uint128*>(pktinfo->ipi6_addr.s6_addr)) =
          send_to_addr_->ip()->ipv6()->address();
    }

    // Set gso_size
    cmsg = CMSG_NXTHDR(msg, cmsg);
    cmsg->cmsg_level = SOL_UDP;
    cmsg->cmsg_type = UDP_GRO;
    cmsg->cmsg_len = CMSG_LEN(sizeof(uint16_t));
    const uint16_t gso_size = 1024;
    *reinterpret_cast<uint16_t*>(CMSG_DATA(cmsg)) = gso_size;

#ifdef SO_RXQ_OVFL
    // Set SO_RXQ_OVFL
    cmsg = CMSG_NXTHDR(msg, cmsg);
    EXPECT_NE(cmsg, nullptr);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SO_RXQ_OVFL;
    cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
    const uint32_t overflow = 0;
    *reinterpret_cast<uint32_t*>(CMSG_DATA(cmsg)) = overflow;
#endif
    return Api::SysCallSizeResult{static_cast<long>(64 * 1024), 0};
  }));

  EXPECT_CALL(listener_callbacks_, onReadReady()).WillOnce(Invoke([&]() { dispatcher_->exit(); }));
  // Only 64 packets should be read, via one recvmsg call.
  EXPECT_CALL(listener_callbacks_, onData(_))
      .Times(64u)
      .WillRepeatedly(Invoke([&](const UdpRecvData& data) -> void {
        validateRecvCallbackParams(data, client_data.size());

        const std::string data_str = data.buffer_->toString();
        EXPECT_EQ(data_str, client_data[num_packets_received_by_listener_ - 1]);
      }));

  EXPECT_CALL(listener_callbacks_, onWriteReady(_)).WillOnce(Invoke([&](const Socket& socket) {
    EXPECT_EQ(&socket.ioHandle(), &server_socket_->ioHandle());
  }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

#endif

} // namespace
} // namespace Network
} // namespace Envoy
