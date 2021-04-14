#include "test/fuzz/fuzz_runner.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/network/udp_listener_impl.h"
#include "common/network/udp_packet_writer_handler_impl.h"
#include "common/network/utility.h"

#include "test/common/network/udp_listener_impl_test_base.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include <fuzzer/FuzzedDataProvider.h>

namespace Envoy {
//	namespace Network {
namespace {

class UdpFuzz;

class fuzzUdpListenerCallbacks : public Network::UdpListenerCallbacks {
 public:
  fuzzUdpListenerCallbacks(UdpFuzz* upf) : my_upf(upf) {}
  ~fuzzUdpListenerCallbacks() = default;
  UdpFuzz* my_upf;
  void onData(Network::UdpRecvData&& data) override;
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;
  void onDataWorker(Network::UdpRecvData&& data) override;
  void post(Network::UdpRecvData&& data) override;
  void onDatagramsDropped(uint32_t dropped) override;
  uint32_t workerIndex() const override;
  Network::UdpPacketWriter& udpPacketWriter() override;
};

class UdpFuzz {
 public:
  UdpFuzz(const uint8_t* buf, size_t len) {
    // Prepare environment
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");
    ip_version_ = TestEnvironment::getIpVersionsForTest()[0];

    server_socket_ = createServerSocket(true, ip_version_);
    server_socket_->addOptions(
        Network::SocketOptionFactory::buildIpPacketInfoOptions());
    server_socket_->addOptions(
        Network::SocketOptionFactory::buildRxQueueOverFlowOptions());

    // Create packet writer
    udp_packet_writer_ =
        std::make_unique<Network::UdpDefaultWriter>(server_socket_->ioHandle());

    // Set up callbacks
    fuzzUdpListenerCallbacks fuzzCallbacks(this);

    // Create listener with default config
    envoy::config::core::v3::UdpSocketConfig config;
    std::unique_ptr<Network::UdpListenerImpl> listener_ =
        std::make_unique<Network::UdpListenerImpl>(
            dispatcherImpl(), server_socket_, fuzzCallbacks,
            dispatcherImpl().timeSource(), config);

    Network::Address::Instance* send_to_addr_ =
        new Network::Address::Ipv4Instance(
            "127.0.0.1",
            server_socket_->addressProvider().localAddress()->ip()->port());

    // Now do all of the fuzzing
    FuzzedDataProvider provider(buf, len);
    total_packets = provider.ConsumeIntegralInRange<uint16_t>(1, 15);
    sent_packets = 0;
    Network::Test::UdpSyncPeer client_(ip_version_);
    // printf("Sending a total of %d\n", total_packets);
    for (uint16_t i = 0; i < total_packets; i++) {
      // printf("Sending \n");
      std::string packet_ = provider.ConsumeBytesAsString(
          provider.ConsumeIntegralInRange<uint32_t>(1, 3000));
      // printf("packet size: %lu\n", packet_.size());
      if (packet_.size() == 0) {
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
    Event::DispatcherImpl* impl =
        dynamic_cast<Event::DispatcherImpl*>(dispatcher_.get());
    // RELEASE_ASSERT(impl, "dispatcher dynamic-cast to DispatcherImpl failed");
    return *impl;
  }

  Network::SocketSharedPtr createServerSocket(
      bool bind, Network::Address::IpVersion version_) {
    // Set IP_FREEBIND to allow sendmsg to send with non-local IPv6 source
    // address.
    return std::make_shared<Network::UdpListenSocket>(
        Network::Test::getCanonicalLoopbackAddress(version_),
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
  uint16_t sent_packets;
  uint16_t total_packets;
  Network::Address::IpVersion ip_version_;
};

void fuzzUdpListenerCallbacks::onData(Network::UdpRecvData&& data) {
  my_upf->sent_packets++;
  if (my_upf->sent_packets == my_upf->total_packets) {
    my_upf->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(data);
  return;
}

void fuzzUdpListenerCallbacks::onReadReady() { return; }

void fuzzUdpListenerCallbacks::onWriteReady(const Network::Socket& socket) {
  UNREFERENCED_PARAMETER(socket);
  return;
}

void fuzzUdpListenerCallbacks::onReceiveError(
    Api::IoError::IoErrorCode error_code) {
  my_upf->sent_packets++;
  if (my_upf->sent_packets == my_upf->total_packets) {
    my_upf->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(error_code);
  return;
}
Network::UdpPacketWriter& fuzzUdpListenerCallbacks::udpPacketWriter() {
  return *my_upf->udp_packet_writer_;
}
uint32_t fuzzUdpListenerCallbacks::workerIndex() const { return 0; }
void fuzzUdpListenerCallbacks::onDataWorker(Network::UdpRecvData&& data) {
  UNREFERENCED_PARAMETER(data);
  return;
}
void fuzzUdpListenerCallbacks::post(Network::UdpRecvData&& data) {
  UNREFERENCED_PARAMETER(data);
  return;
}

void fuzzUdpListenerCallbacks::onDatagramsDropped(uint32_t dropped) {
  my_upf->sent_packets++;
  if (my_upf->sent_packets == my_upf->total_packets) {
    my_upf->dispatcher_->exit();
  }
  UNREFERENCED_PARAMETER(dropped);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  UdpFuzz udp_instance(buf, len);
}
}  // namespace
//} // Network
}  // Envoy
