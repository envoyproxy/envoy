#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/registry/registry.h"

#include "source/common/network/io_socket_error_impl.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/udp/health_checker_impl.h"

#include "test/common/upstream/health_checker_test_base.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/socket.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Udp {
namespace {

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

constexpr uint32_t MaxSendAttemptsPerInvocation = 16;

Api::IoCallUint64Result ioSuccess(uint64_t bytes) { return {bytes, Api::IoError::none()}; }

Api::IoCallUint64Result ioAgain() { return {0, Network::IoSocketError::getIoSocketEagainError()}; }

Api::IoCallUint64Result ioError() {
  return {0, Network::IoSocketError::create(SOCKET_ERROR_CONNRESET)};
}

Api::IoCallUint64Result ioMessageTooBig() {
  return {0, Network::IoSocketError::create(SOCKET_ERROR_MSG_SIZE)};
}

Api::IoCallUint64Result ioInterrupt() {
  return {0, Network::IoSocketError::create(SOCKET_ERROR_INTR)};
}

auto receiveDatagram(std::string payload) {
  return
      [payload = std::move(payload)](void* buffer, size_t length, int) -> Api::IoCallUint64Result {
        const size_t bytes = std::min(length, payload.size());
        if (bytes != 0) {
          std::memcpy(buffer, payload.data(), bytes);
        }
        return ioSuccess(bytes);
      };
}

class TrackedMockSocket : public Network::MockSocket {
public:
  explicit TrackedMockSocket(std::shared_ptr<bool> destroyed) : destroyed_(std::move(destroyed)) {}
  ~TrackedMockSocket() override { *destroyed_ = true; }

private:
  const std::shared_ptr<bool> destroyed_;
};

class TestUdpHealthCheckerImpl : public Upstream::UdpHealthCheckerImpl {
public:
  using Upstream::UdpHealthCheckerImpl::UdpHealthCheckerImpl;

  std::deque<Network::SocketPtr> sockets_;
  std::vector<Network::Address::InstanceConstSharedPtr> created_addresses_;

protected:
  Network::SocketPtr
  createSocket(const Network::Address::InstanceConstSharedPtr& address) override {
    created_addresses_.push_back(address);
    if (sockets_.empty()) {
      return nullptr;
    }
    Network::SocketPtr socket = std::move(sockets_.front());
    sockets_.pop_front();
    return socket;
  }
};

class UdpHealthCheckerTest : public testing::Test,
                             public Upstream::HealthCheckerTestBase,
                             public Event::TestUsingSimulatedTime {
protected:
  struct SocketState {
    std::shared_ptr<bool> destroyed_{std::make_shared<bool>(false)};
    Network::MockSocket* socket_{};
    Network::MockIoHandle* io_handle_{};
    Event::FileReadyCb file_ready_cb_;
  };

  void initialize(std::string send = std::string("\x01\x02", 2),
                  std::string receive = std::string("\x03\x04", 2),
                  uint32_t unhealthy_threshold = 1, uint32_t healthy_threshold = 1) {
    envoy::config::core::v3::HealthCheck health_check;
    health_check.mutable_timeout()->set_seconds(1);
    health_check.mutable_interval()->set_seconds(1);
    health_check.mutable_unhealthy_threshold()->set_value(unhealthy_threshold);
    health_check.mutable_healthy_threshold()->set_value(healthy_threshold);
    health_check.mutable_custom_health_check()->set_name("envoy.health_checkers.udp");

    envoy::extensions::health_checkers::udp::v3::UdpHealthCheck udp_config;
    udp_config.mutable_send()->set_binary(std::move(send));
    udp_config.mutable_receive()->set_binary(std::move(receive));

    health_checker_ = std::make_shared<TestUdpHealthCheckerImpl>(
        *cluster_, health_check, udp_config, dispatcher_, runtime_, random_,
        Upstream::HealthCheckEventLoggerPtr(event_logger_storage_.release()));

    envoy::config::endpoint::v3::Endpoint::HealthCheckConfig health_check_config;
    auto* socket_address = health_check_config.mutable_address()->mutable_socket_address();
    socket_address->set_address("127.0.0.2");
    socket_address->set_port_value(8080);
    host_ = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80", health_check_config);
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host_};
  }

  SocketState& queueSocket(Api::SysCallIntResult connect_result = {0, 0}, bool open = true,
                           bool expect_connect = true) {
    auto state = std::make_unique<SocketState>();
    auto socket = std::make_unique<NiceMock<TrackedMockSocket>>(state->destroyed_);
    state->socket_ = socket.get();
    state->io_handle_ = socket->io_handle_.get();

    EXPECT_CALL(*state->io_handle_, enableFileEvents(0)).Times(testing::AtMost(1));
    EXPECT_CALL(*state->socket_, isOpen()).WillOnce(Return(open));
    if (open && expect_connect) {
      EXPECT_CALL(*state->socket_, connect(host_->healthCheckAddress()))
          .WillOnce(Return(connect_result));
      if (connect_result.return_value_ == 0) {
        EXPECT_CALL(*state->io_handle_,
                    createFileEvent_(Ref(dispatcher_), _, Event::PlatformDefaultTriggerType, 0))
            .WillOnce(SaveArg<1>(&state->file_ready_cb_));
      }
    }

    health_checker_->sockets_.push_back(std::move(socket));
    socket_states_.push_back(std::move(state));
    return *socket_states_.back();
  }

  void expectSession() {
    InSequence sequence;
    interval_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    timeout_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    deferred_callback_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  }

  void expectNetworkFailureLogs() {
    EXPECT_CALL(event_logger_, logEjectUnhealthy(envoy::data::core::v3::UDP, _,
                                                 envoy::data::core::v3::NETWORK, _));
    EXPECT_CALL(event_logger_, logUnhealthy(envoy::data::core::v3::UDP, _,
                                            envoy::data::core::v3::NETWORK, true, _));
  }

  void expectSend(SocketState& state, const std::string& expected) {
    EXPECT_CALL(*state.io_handle_, send(_, expected.size()))
        .WillOnce(Invoke([expected](const void* data, size_t length) {
          if (length == 0) {
            EXPECT_TRUE(expected.empty());
          } else {
            EXPECT_EQ(expected, std::string(static_cast<const char*>(data), length));
          }
          return ioSuccess(length);
        }));
    EXPECT_CALL(*state.io_handle_,
                enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Closed));
  }

  void startAndSend(SocketState& state, const std::string& expected = std::string("\x01\x02", 2)) {
    expectSend(state, expected);
    expectSession();
    health_checker_->start();
    deferred_callback_->invokeCallback();
  }

  uint64_t counter(const std::string& name) const {
    return cluster_->info_->stats_store_.counter("health_check." + name).value();
  }

  std::shared_ptr<TestUdpHealthCheckerImpl> health_checker_;
  Upstream::HostSharedPtr host_;
  std::vector<std::unique_ptr<SocketState>> socket_states_;
  Event::MockTimer* interval_timer_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockSchedulableCallback* deferred_callback_{};
};

TEST_F(UdpHealthCheckerTest, StartsConnectedAttemptAndSendsOneDatagram) {
  initialize();
  SocketState& state = queueSocket();
  expectSend(state, std::string("\x01\x02", 2));
  expectSession();

  health_checker_->start();

  ASSERT_EQ(1, health_checker_->created_addresses_.size());
  EXPECT_EQ(host_->healthCheckAddress(), health_checker_->created_addresses_[0]);
  EXPECT_EQ("127.0.0.2:8080", health_checker_->created_addresses_[0]->asString());
  EXPECT_EQ(1, counter("attempt"));
  EXPECT_EQ(0, counter("success"));
  EXPECT_TRUE(timeout_timer_->enabled());

  deferred_callback_->invokeCallback();
  EXPECT_EQ(0, counter("success"));
}

TEST_F(UdpHealthCheckerTest, UsesClusterUpstreamLocalAddress) {
  initialize();
  cluster_->info_->source_address_ = host_->address();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.socket_, bind(cluster_->info_->source_address_))
      .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  expectSend(state, std::string("\x01\x02", 2));
  expectSession();

  health_checker_->start();
  deferred_callback_->invokeCallback();
}

TEST_F(UdpHealthCheckerTest, ReleasesPreviousSocketBeforeRebindingFixedLocalPort) {
  initialize();
  cluster_->info_->source_address_ = host_->address();
  SocketState& first = queueSocket();
  EXPECT_CALL(*first.socket_, bind(cluster_->info_->source_address_))
      .WillOnce(Return(Api::SysCallIntResult{0, 0}));
  startAndSend(first);

  EXPECT_CALL(*first.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(first.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_FALSE(*first.destroyed_);

  SocketState& second = queueSocket();
  EXPECT_CALL(*second.socket_, bind(cluster_->info_->source_address_))
      .WillOnce(Invoke([destroyed = first.destroyed_](const auto&) {
        EXPECT_TRUE(*destroyed);
        return Api::SysCallIntResult{0, 0};
      }));
  expectSend(second, std::string("\x01\x02", 2));
  interval_timer_->invokeCallback();
  deferred_callback_->invokeCallback();
}

TEST_F(UdpHealthCheckerTest, ExactDatagramSucceeds) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_EQ(1, counter("success"));
  EXPECT_EQ(0, counter("failure"));
  EXPECT_FALSE(timeout_timer_->enabled());
  EXPECT_TRUE(interval_timer_->enabled());
}

TEST_F(UdpHealthCheckerTest, IgnoresMismatchesUntilExactDatagramArrives) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x05", 2))))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03", 1))))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04\x05", 3))))
      .WillOnce(Invoke([](void*, size_t, int) { return ioMessageTooBig(); }))
      .WillOnce(Invoke([](void*, size_t, int) { return ioAgain(); }));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(0, counter("success"));
  EXPECT_EQ(0, counter("failure"));
  EXPECT_TRUE(timeout_timer_->enabled());

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(1, counter("success"));
}

TEST_F(UdpHealthCheckerTest, RetriesWouldBlockSendWithoutSendingTwice) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, 2))
      .WillOnce(Invoke([](const void*, size_t) { return ioAgain(); }))
      .WillOnce(Invoke([](const void*, size_t length) { return ioSuccess(length); }));
  EXPECT_CALL(*state.io_handle_,
              enableFileEvents(Event::FileReadyType::Write | Event::FileReadyType::Closed));
  EXPECT_CALL(*state.io_handle_,
              enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Closed));
  expectSession();
  health_checker_->start();
  deferred_callback_->invokeCallback();

  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Write).ok());

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(1, counter("success"));
}

TEST_F(UdpHealthCheckerTest, RetriesInterruptedSendAndReceive) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, 2))
      .WillOnce(Invoke([](const void*, size_t) { return ioInterrupt(); }))
      .WillOnce(Invoke([](const void*, size_t length) { return ioSuccess(length); }));
  EXPECT_CALL(*state.io_handle_,
              enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Closed));
  expectSession();
  health_checker_->start();
  deferred_callback_->invokeCallback();

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke([](void*, size_t, int) { return ioInterrupt(); }))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(1, counter("success"));
}

TEST_F(UdpHealthCheckerTest, ReactivatesWriteAfterBoundedInterrupts) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, 2))
      .Times(MaxSendAttemptsPerInvocation)
      .WillRepeatedly(Invoke([](const void*, size_t) { return ioInterrupt(); }));
  EXPECT_CALL(*state.io_handle_,
              enableFileEvents(Event::FileReadyType::Write | Event::FileReadyType::Closed));
  EXPECT_CALL(*state.io_handle_, activateFileEvents(Event::FileReadyType::Write));
  expectSession();
  health_checker_->start();
  deferred_callback_->invokeCallback();

  expectSend(state, std::string("\x01\x02", 2));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Write).ok());
}

TEST_F(UdpHealthCheckerTest, ReactivatesReadAfterBoundedBatch) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .Times(Network::NUM_DATAGRAMS_PER_RECEIVE)
      .WillRepeatedly(Invoke(receiveDatagram(std::string("\x03\x05", 2))));
  EXPECT_CALL(*state.io_handle_, activateFileEvents(Event::FileReadyType::Read));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(1, counter("success"));
}

TEST_F(UdpHealthCheckerTest, TimeoutIsNetworkTimeoutFailure) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(event_logger_, logEjectUnhealthy(envoy::data::core::v3::UDP, _,
                                               envoy::data::core::v3::NETWORK_TIMEOUT, _));
  EXPECT_CALL(event_logger_, logUnhealthy(envoy::data::core::v3::UDP, _,
                                          envoy::data::core::v3::NETWORK_TIMEOUT, true, _));
  timeout_timer_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
  EXPECT_TRUE(host_->healthFlagGet(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT));
}

TEST_F(UdpHealthCheckerTest, TimeoutBeforeDeferredSendDoesNotSend) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, _)).Times(0);
  expectSession();
  health_checker_->start();

  EXPECT_CALL(*deferred_callback_, cancel());
  EXPECT_CALL(event_logger_, logEjectUnhealthy(envoy::data::core::v3::UDP, _,
                                               envoy::data::core::v3::NETWORK_TIMEOUT, _));
  EXPECT_CALL(event_logger_, logUnhealthy(envoy::data::core::v3::UDP, _,
                                          envoy::data::core::v3::NETWORK_TIMEOUT, true, _));
  timeout_timer_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
  EXPECT_TRUE(host_->healthFlagGet(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT));
}

TEST_F(UdpHealthCheckerTest, ClosedEventIsNetworkFailure) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  expectNetworkFailureLogs();
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Closed).ok());
  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, PartialSendIsNetworkFailure) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, 2)).WillOnce(Invoke([](const void*, size_t) {
    return ioSuccess(1);
  }));
  expectSession();
  health_checker_->start();
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, NullSocketIsDeferredNetworkFailure) {
  initialize();
  expectSession();

  health_checker_->start();
  EXPECT_EQ(0, counter("failure"));
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, ClosedSocketIsDeferredNetworkFailure) {
  initialize();
  SocketState& state = queueSocket({0, 0}, false);
  EXPECT_CALL(*state.socket_, connect(_)).Times(0);
  EXPECT_CALL(*state.io_handle_, createFileEvent_(_, _, _, _)).Times(0);
  expectSession();

  health_checker_->start();
  EXPECT_EQ(0, counter("failure"));
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, BindFailureIsDeferredNetworkFailure) {
  initialize();
  cluster_->info_->source_address_ = host_->address();
  SocketState& state = queueSocket({0, 0}, true, false);
  EXPECT_CALL(*state.socket_, bind(cluster_->info_->source_address_))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_ADDR_NOT_AVAIL}));
  EXPECT_CALL(*state.socket_, connect(_)).Times(0);
  expectSession();

  health_checker_->start();
  EXPECT_EQ(0, counter("failure"));
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, SocketOptionFailureIsDeferredNetworkFailure) {
  initialize();
  auto option = std::make_shared<NiceMock<Network::MockSocketOption>>();
  auto options = std::make_shared<Network::Socket::Options>();
  options->push_back(option);
  ON_CALL(*cluster_->info_->upstream_local_address_selector_, getUpstreamLocalAddressImpl(_, _))
      .WillByDefault(Return(Upstream::UpstreamLocalAddress{nullptr, options}));
  SocketState& state = queueSocket({0, 0}, true, false);
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(false));
  EXPECT_CALL(*state.socket_, connect(_)).Times(0);
  expectSession();

  health_checker_->start();
  EXPECT_EQ(0, counter("failure"));
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
}

TEST_F(UdpHealthCheckerTest, ConnectFailureIsDeferredAndReportedOnce) {
  initialize();
  queueSocket({-1, SOCKET_ERROR_CONNRESET});
  expectSession();

  health_checker_->start();
  EXPECT_TRUE(timeout_timer_->enabled());
  EXPECT_EQ(0, counter("failure"));

  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();
  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
  EXPECT_FALSE(timeout_timer_->enabled());
}

TEST_F(UdpHealthCheckerTest, SendFailureIsDeferredAndReportedOnce) {
  initialize();
  SocketState& state = queueSocket();
  EXPECT_CALL(*state.io_handle_, send(_, 2)).WillOnce(Invoke([](const void*, size_t) {
    return ioError();
  }));
  expectSession();

  health_checker_->start();
  EXPECT_EQ(0, counter("failure"));
  expectNetworkFailureLogs();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
  EXPECT_FALSE(timeout_timer_->enabled());
}

TEST_F(UdpHealthCheckerTest, ReceiveSocketErrorIsNetworkFailure) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0)).WillOnce(Invoke([](void*, size_t, int) {
    return ioError();
  }));
  expectNetworkFailureLogs();
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_EQ(1, counter("failure"));
  EXPECT_EQ(1, counter("network_failure"));
  EXPECT_FALSE(host_->healthFlagGet(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT));
}

TEST_F(UdpHealthCheckerTest, StaleReadEventAfterSuccessIsIgnored) {
  initialize();
  SocketState& state = queueSocket();
  startAndSend(state);

  EXPECT_CALL(*state.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_EQ(1, counter("success"));
  EXPECT_EQ(0, counter("failure"));
}

TEST_F(UdpHealthCheckerTest, CreatesFreshSocketForEveryAttempt) {
  initialize();
  SocketState& first = queueSocket();
  Network::MockSocket* first_socket = first.socket_;
  startAndSend(first);

  EXPECT_CALL(*first.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(first.file_ready_cb_(Event::FileReadyType::Read).ok());

  SocketState& second = queueSocket();
  Network::MockSocket* second_socket = second.socket_;
  EXPECT_NE(first_socket, second_socket);
  expectSend(second, std::string("\x01\x02", 2));
  interval_timer_->invokeCallback();
  deferred_callback_->invokeCallback();

  EXPECT_EQ(2, health_checker_->created_addresses_.size());
  EXPECT_EQ(2, counter("attempt"));
}

TEST_F(UdpHealthCheckerTest, RetainsPreviousSocketAcrossReplacementConnectFailure) {
  initialize();
  SocketState& first = queueSocket();
  startAndSend(first);

  EXPECT_CALL(*first.io_handle_, recv(_, 3, 0))
      .WillOnce(Invoke(receiveDatagram(std::string("\x03\x04", 2))));
  EXPECT_TRUE(first.file_ready_cb_(Event::FileReadyType::Read).ok());

  SocketState& second = queueSocket({-1, SOCKET_ERROR_CONNRESET});
  interval_timer_->invokeCallback();
  EXPECT_FALSE(*first.destroyed_);
  EXPECT_FALSE(*second.destroyed_);
  EXPECT_CALL(event_logger_,
              logEjectUnhealthy(envoy::data::core::v3::UDP, _, envoy::data::core::v3::NETWORK, _));
  deferred_callback_->invokeCallback();

  EXPECT_FALSE(*first.destroyed_);
  EXPECT_TRUE(*second.destroyed_);
  EXPECT_EQ(1, counter("failure"));
}

TEST_F(UdpHealthCheckerTest, SupportsEmptyBinaryDatagrams) {
  initialize("", "");
  SocketState& state = queueSocket();
  startAndSend(state, "");

  EXPECT_CALL(*state.io_handle_, recv(_, 1, 0)).WillOnce(Invoke(receiveDatagram("")));
  EXPECT_TRUE(state.file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(1, counter("success"));
}

TEST_F(UdpHealthCheckerTest, HostRemovalCancelsPendingAttempt) {
  initialize();
  SocketState& state = queueSocket();
  expectSession();
  health_checker_->start();

  EXPECT_CALL(*deferred_callback_, cancel());
  EXPECT_CALL(*state.io_handle_, enableFileEvents(0));
  Upstream::HostVector removed{host_};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);

  EXPECT_EQ(0, counter("success"));
  EXPECT_EQ(0, counter("failure"));
}

TEST(UdpHealthCheckerFactoryTest, CreatesRegisteredFactory) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.udp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.udp.v3.UdpHealthCheck
        send:
          text: "0102"
        receive:
          text: "0304"
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  ON_CALL(context.api_, randomGenerator()).WillByDefault(ReturnRef(context.random_));

  Upstream::UdpHealthCheckerFactory factory;
  EXPECT_EQ("envoy.health_checkers.udp", factory.name());
  EXPECT_EQ("envoy.extensions.health_checkers.udp.v3.UdpHealthCheck",
            factory.createEmptyConfigProto()->GetDescriptor()->full_name());
  EXPECT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::CustomHealthCheckerFactory>::getFactory(
          factory.name()));
  EXPECT_NE(
      nullptr,
      dynamic_cast<Upstream::ProdUdpHealthCheckerImpl*>(
          factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context)
              .get()));
}

TEST(UdpHealthCheckerFactoryTest, RejectsInvalidHexPayload) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.udp
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.udp.v3.UdpHealthCheck
        send:
          text: "not-hex"
        receive:
          text: "0304"
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;
  ON_CALL(context.api_, randomGenerator()).WillByDefault(ReturnRef(context.random_));
  Upstream::UdpHealthCheckerFactory factory;

  EXPECT_THROW(
      factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context),
      EnvoyException);
}

} // namespace
} // namespace Udp
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
