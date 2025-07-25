#include <fuzzer/FuzzedDataProvider.h>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"

#include "test/common/network/udp_listener_impl_test_base.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

using testing::Return;

namespace Envoy {
namespace {

class OverrideOsSysCallsImpl : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
  MOCK_METHOD(bool, supportsMmsg, (), (const));
};

class UdpFuzz;

class FuzzUdpListenerCallbacks : public Network::UdpListenerCallbacks {
public:
  FuzzUdpListenerCallbacks(UdpFuzz* upf) : my_upf_(upf) {}
  ~FuzzUdpListenerCallbacks() override = default;
  void onData(Network::UdpRecvData&& data) override;
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;
  void onDataWorker(Network::UdpRecvData&& data) override;
  void post(Network::UdpRecvData&& data) override;
  void onDatagramsDropped(uint32_t dropped) override;
  uint32_t workerIndex() const override;
  Network::UdpPacketWriter& udpPacketWriter() override;
  size_t numPacketsExpectedPerEventLoop() const override;
  const Network::IoHandle::UdpSaveCmsgConfig& udpSaveCmsgConfig() const override;

private:
  UdpFuzz* my_upf_;
};

class UdpFuzz {
public:
  UdpFuzz(const uint8_t* buf, size_t len) {
    // Prepare environment
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");
    ip_version_ = TestEnvironment::getIpVersionsForTest()[0];

    server_socket_ = createServerSocket(true, ip_version_);
    server_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    EXPECT_TRUE(Network::Socket::applyOptions(server_socket_->options(), *server_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));

    // Create packet writer
    udp_packet_writer_ = std::make_unique<Network::UdpDefaultWriter>(server_socket_->ioHandle());

    // Set up callbacks
    FuzzUdpListenerCallbacks fuzzCallbacks(this);

    // Create listener with default config
    envoy::config::core::v3::UdpSocketConfig config;

    FuzzedDataProvider provider(buf, len);
    uint16_t SocketType = provider.ConsumeIntegralInRange<uint16_t>(0, 2);
    if (SocketType == 0) {
      config.mutable_prefer_gro()->set_value(true);
      ON_CALL(override_syscall_, supportsUdpGro()).WillByDefault(Return(true));
    } else if (SocketType == 1) {
      ON_CALL(override_syscall_, supportsMmsg()).WillByDefault(Return(true));
    } else {
      ON_CALL(override_syscall_, supportsMmsg()).WillByDefault(Return(false));
      ON_CALL(override_syscall_, supportsUdpGro()).WillByDefault(Return(false));
    }

    std::unique_ptr<Network::UdpListenerImpl> listener_ =
        std::make_unique<Network::UdpListenerImpl>(dispatcherImpl(), server_socket_, fuzzCallbacks,
                                                   dispatcherImpl().timeSource(), config);

    Network::Address::Instance* send_to_addr_ = new Network::Address::Ipv4Instance(
        "127.0.0.1", server_socket_->connectionInfoProvider().localAddress()->ip()->port());

    // Now do all of the fuzzing
    static const int MaxPackets = 15;
    total_packets_ = provider.ConsumeIntegralInRange<uint16_t>(1, MaxPackets);
    Network::Test::UdpSyncPeer client_(ip_version_);
    for (uint16_t i = 0; i < total_packets_; i++) {
      std::string packet_ =
          provider.ConsumeBytesAsString(provider.ConsumeIntegralInRange<uint32_t>(1, 3000));
      if (packet_.empty()) {
        packet_ = "EMPTY_PACKET";
      }
      client_.write(packet_, *send_to_addr_);
    }
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    // cleanup
    delete send_to_addr_;
  }

  Event::DispatcherImpl& dispatcherImpl() {
    // We need access to the concrete impl type in order to instantiate a
    // Test[Udp]Listener, which instantiates a [Udp]ListenerImpl, which requires
    // a DispatcherImpl to access DispatcherImpl::base_, which is not part of
    // the Dispatcher API.
    Event::DispatcherImpl* impl = dynamic_cast<Event::DispatcherImpl*>(dispatcher_.get());
    return *impl;
  }

  Network::SocketSharedPtr createServerSocket(bool bind, Network::Address::IpVersion version) {
    // Set IP_FREEBIND to allow sendmsg to send with non-local IPv6 source
    // address.
    return std::make_shared<Network::UdpListenSocket>(
        Network::Test::getCanonicalLoopbackAddress(version),
#ifdef IP_FREEBIND
        Network::SocketOptionFactory::buildIpFreebindOptions(),
#else
        nullptr,
#endif
        bind);
  }

  Network::SocketSharedPtr server_socket_;
  Event::DispatcherPtr dispatcher_;
  Api::ApiPtr api_;
  Network::UdpPacketWriterPtr udp_packet_writer_;
  uint16_t sent_packets_ = 0;
  uint16_t total_packets_;
  Network::Address::IpVersion ip_version_;
  NiceMock<OverrideOsSysCallsImpl> override_syscall_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&override_syscall_};
};

void FuzzUdpListenerCallbacks::onData(Network::UdpRecvData&& data) {
  my_upf_->sent_packets_++;
  if (my_upf_->sent_packets_ == my_upf_->total_packets_) {
    my_upf_->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(data);
}

void FuzzUdpListenerCallbacks::onReadReady() {}

void FuzzUdpListenerCallbacks::onWriteReady(const Network::Socket& socket) {
  UNREFERENCED_PARAMETER(socket);
}

void FuzzUdpListenerCallbacks::onReceiveError(Api::IoError::IoErrorCode error_code) {
  my_upf_->sent_packets_++;
  if (my_upf_->sent_packets_ == my_upf_->total_packets_) {
    my_upf_->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(error_code);
}
Network::UdpPacketWriter& FuzzUdpListenerCallbacks::udpPacketWriter() {
  return *my_upf_->udp_packet_writer_;
}
uint32_t FuzzUdpListenerCallbacks::workerIndex() const { return 0; }
void FuzzUdpListenerCallbacks::onDataWorker(Network::UdpRecvData&& data) {
  UNREFERENCED_PARAMETER(data);
}
void FuzzUdpListenerCallbacks::post(Network::UdpRecvData&& data) { UNREFERENCED_PARAMETER(data); }

void FuzzUdpListenerCallbacks::onDatagramsDropped(uint32_t dropped) {
  my_upf_->sent_packets_++;
  if (my_upf_->sent_packets_ == my_upf_->total_packets_) {
    my_upf_->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(dropped);
}

size_t FuzzUdpListenerCallbacks::numPacketsExpectedPerEventLoop() const {
  return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
}

const Network::IoHandle::UdpSaveCmsgConfig& FuzzUdpListenerCallbacks::udpSaveCmsgConfig() const {
  static const Network::IoHandle::UdpSaveCmsgConfig empty_config{};
  return empty_config;
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) { UdpFuzz udp_instance(buf, len); }
} // namespace
} // namespace Envoy
