#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <memory>
#include <string>
#include <thread>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"
#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_socket_interface.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;
namespace {

using DynamicModuleSocketInterfaceProto =
    envoy::extensions::network::socket_interface::dynamic_modules::v3::DynamicModuleSocketInterface;

constexpr uint32_t ModuleRead = envoy_dynamic_module_type_socket_interface_io_event_Read;
constexpr uint32_t ModuleWrite = envoy_dynamic_module_type_socket_interface_io_event_Write;

class DynamicModuleSocketInterfaceTest : public testing::Test {
public:
  DynamicModuleSocketInterfaceTest() : api_(Api::createApiForTest()) {
    context_.options_.hot_restart_disabled_ = true;
  }

  void SetUp() override { dispatcher_ = api_->allocateDispatcher("test"); }

  void TearDown() override {
    // The switches are module-global and the module stays loaded across tests.
    if (module_.ok() && module_.value() != nullptr) {
      for (const char* name :
           {"setSocketInterfaceTestForceBadFd", "setSocketInterfaceTestFailIo",
            "setSocketInterfaceTestListenMode", "setSocketInterfaceTestReportAcceptAddresses",
            "setSocketInterfaceTestByteReport", "setSocketInterfaceTestCreateFlagsMode",
            "setSocketInterfaceTestAcceptMode"}) {
        auto setter = module_.value()->getFunctionPointer<void (*)(int)>(name);
        if (setter.ok()) {
          setter.value()(0);
        }
      }
      auto greeting = module_.value()->getFunctionPointer<void (*)(const char*)>(
          "setSocketInterfaceTestGreeting");
      if (greeting.ok()) {
        greeting.value()(nullptr);
      }
    }
  }

  // Loads the module and installs it as the extension of the interface under test. The module is
  // loaded once more with do_not_close so the test helpers it exports stay callable through the
  // same dlopen handle the extension uses.
  void installInterface(const std::string& module_name, const std::string& config = "") {
    const std::string path = Extensions::DynamicModules::testSharedObjectPath(module_name, "c");
    module_ = Extensions::DynamicModules::newDynamicModule(path, /*do_not_close=*/true);
    ASSERT_TRUE(module_.ok()) << module_.status().message();

    DynamicModuleSocketInterfaceProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        path);
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_socket_interface_name("socket_interface_test");
    if (!config.empty()) {
      Protobuf::StringValue config_value;
      config_value.set_value(config);
      ASSERT_TRUE(proto_config.mutable_socket_interface_config()->PackFrom(config_value));
    }
    auto result = interface_.createBootstrapExtension(proto_config, context_);
    ASSERT_TRUE(result.ok()) << result.status().message();
    extension_ = std::move(result.value());
  }

  template <class T> T moduleFunction(const std::string& name) {
    auto function = module_.value()->getFunctionPointer<T>(name);
    EXPECT_TRUE(function.ok()) << function.status().message();
    return function.value();
  }
  void setSwitch(const std::string& name, int value) { moduleFunction<void (*)(int)>(name)(value); }
  int moduleCounter(const std::string& name) { return moduleFunction<int (*)(void)>(name)(); }
  uint64_t counter(const std::string& name) {
    auto found = TestUtility::findCounter(
        context_.store_,
        absl::StrCat("dynamic_modules.socket_interface.socket_interface_test.", name));
    return found == nullptr ? 0 : found->value();
  }

  Network::IoHandlePtr
  makeSocket(uint32_t port, Network::Socket::Type socket_type = Network::Socket::Type::Stream) {
    auto addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", port);
    return interface_.socket(socket_type, addr, Network::SocketCreationOptions{});
  }

  void registerHandle(Network::IoHandle& handle, uint32_t events,
                      Event::FileTriggerType trigger = Event::FileTriggerType::Edge) {
    handle.initializeFileEvent(
        *dispatcher_,
        [this](uint32_t fired) -> absl::Status {
          fired_events_ |= fired;
          return absl::OkStatus();
        },
        trigger, events);
  }
  // Runs the loop until nothing is ready, so injected events posted from callbacks run too.
  uint32_t runAndTakeEvents() {
    fired_events_ = 0;
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    return fired_events_;
  }
  // Delivers the kernel's first Write, which completes the connect a delegated client socket
  // holds module readiness for.
  void completeConnect(Network::IoHandle& handle) {
    registerHandle(handle, Event::FileReadyType::Read | Event::FileReadyType::Write);
    EXPECT_TRUE(runAndTakeEvents() & Event::FileReadyType::Write);
  }

  static void expectFailedSocket(Network::IoHandle& handle) {
    EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(&handle));
    EXPECT_EQ(SOCKET_ERROR_NOT_SUP,
              handle.bind(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0)).errno_);
    EXPECT_EQ(SOCKET_ERROR_NOT_SUP, handle.listen(1).errno_);
    EXPECT_EQ(
        SOCKET_ERROR_NOT_SUP,
        handle.connect(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1)).errno_);
    Buffer::OwnedImpl buffer("x");
    EXPECT_FALSE(handle.write(buffer).ok());
    EXPECT_FALSE(handle.send("x", 1).ok());
    char byte;
    EXPECT_FALSE(handle.recv(&byte, 1, 0).ok());
    Buffer::RawSlice slice{&byte, 1};
    EXPECT_FALSE(handle.readv(1, &slice, 1).ok());
    Network::Address::Ipv4Instance peer("127.0.0.1", 1);
    EXPECT_FALSE(handle.sendmsg(&slice, 1, 0, nullptr, peer).ok());
    Network::IoHandle::RecvMsgOutput output(1, nullptr);
    EXPECT_FALSE(handle.recvmsg(&slice, 1, 0, {}, output).ok());
    Envoy::RawSliceArrays slice_arrays(1, absl::FixedArray<Buffer::RawSlice>(1, slice));
    EXPECT_FALSE(handle.recvmmsg(slice_arrays, 0, {}, output).ok());
    sockaddr_storage addr = {};
    socklen_t addr_len = sizeof(addr);
    EXPECT_EQ(nullptr, handle.accept(reinterpret_cast<sockaddr*>(&addr), &addr_len));
    EXPECT_NE(nullptr, handle.duplicate());
    EXPECT_TRUE(handle.close().ok());
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  DynamicModuleSocketInterface interface_;
  Server::BootstrapExtensionPtr extension_;
  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> module_;
  uint32_t fired_events_{0};
};

// =============================================================================
// Data path
// =============================================================================

TEST_F(DynamicModuleSocketInterfaceTest, DelegatesDataPathAndSkipsConnect) {
  installInterface("socket_interface_test");

  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));

  // The module handed Envoy an already-connected socket.
  auto connect_result =
      handle->connect(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1000));
  EXPECT_EQ(0, connect_result.return_value_);
  EXPECT_EQ(0, connect_result.errno_);
  // Bind and listen stay with the kernel, which refuses them on a connected socket.
  EXPECT_EQ(
      -1,
      handle->bind(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0)).return_value_);
  EXPECT_EQ(-1, handle->listen(1).return_value_);

  Buffer::OwnedImpl buffer("hello");
  auto write_result = handle->write(buffer);
  ASSERT_TRUE(write_result.ok());
  EXPECT_EQ(5, write_result.return_value_);
  EXPECT_EQ(1, handle->send("!", 1).return_value_);
  char received[8] = {0};
  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");
  ASSERT_GE(peer_fd, 0);
  EXPECT_EQ(6, ::recv(peer_fd, received, sizeof(received), 0));
  EXPECT_EQ("hello!", std::string(received, 6));

  ASSERT_EQ(3, ::send(peer_fd, "abc", 3, 0));
  char sent[4] = {0};
  Buffer::RawSlice slice{sent, sizeof(sent)};
  auto read_result = handle->readv(sizeof(sent), &slice, 1);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ(3, read_result.return_value_);
  EXPECT_EQ("abc", std::string(sent, 3));

  EXPECT_TRUE(handle->readv(sizeof(sent), &slice, 1).wouldBlock());

  // The counter is module-global, so compare against its starting value.
  const int initial_close_count = moduleCounter("getSocketInterfaceTestCloseCount");
  EXPECT_TRUE(handle->close().ok());
  EXPECT_EQ(initial_close_count + 1, moduleCounter("getSocketInterfaceTestCloseCount"));
  // Closing twice does not release the module state twice.
  EXPECT_TRUE(handle->close().ok());
  EXPECT_EQ(initial_close_count + 1, moduleCounter("getSocketInterfaceTestCloseCount"));
}

TEST_F(DynamicModuleSocketInterfaceTest, RecvGoesThroughTheModuleAndPeekKeepsTheBytes) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestByteReport", 0);
  // The greeting lives in the module's memory, which only the module's read hook can return.
  moduleFunction<void (*)(const char*)>("setSocketInterfaceTestGreeting")("held");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);

  char buffer[8] = {0};
  auto peek_result = handle->recv(buffer, sizeof(buffer), MSG_PEEK);
  ASSERT_TRUE(peek_result.ok());
  EXPECT_EQ("held", std::string(buffer, peek_result.return_value_));
  // The peek left the bytes with the module, so a read returns them again.
  memset(buffer, 0, sizeof(buffer));
  auto read_result = handle->recv(buffer, sizeof(buffer), 0);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ("held", std::string(buffer, read_result.return_value_));
  EXPECT_TRUE(handle->recv(buffer, sizeof(buffer), 0).wouldBlock());
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, DestructorReleasesModuleState) {
  installInterface("socket_interface_test");
  const int initial_close_count = moduleCounter("getSocketInterfaceTestCloseCount");
  {
    ASSERT_NE(nullptr, makeSocket(1000));
  }
  // Destroying the handle without closing it still releases the module state.
  EXPECT_EQ(initial_close_count + 1, moduleCounter("getSocketInterfaceTestCloseCount"));
}

TEST_F(DynamicModuleSocketInterfaceTest, ShutdownDelegatesToModule) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);

  auto result = handle->shutdown(SHUT_WR);
  EXPECT_EQ(0, result.return_value_);
  EXPECT_EQ(0, result.errno_);

  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");
  char received[8];
  EXPECT_EQ(0, ::recv(peer_fd, received, sizeof(received), 0));
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, ShutdownWithoutModuleHookSendsFin) {
  installInterface("socket_interface_missing_io_shutdown");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));

  auto result = handle->shutdown(SHUT_WR);
  EXPECT_EQ(0, result.return_value_);
  EXPECT_EQ(0, result.errno_);
  // The file descriptor shutdown reached the peer as EOF.
  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");
  char received[8];
  EXPECT_EQ(0, ::recv(peer_fd, received, sizeof(received), 0));
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, IoErrorMapsToConnectionReset) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  setSwitch("setSocketInterfaceTestFailIo", 1);

  char buf[4];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  auto read_result = handle->readv(sizeof(buf), &slice, 1);
  EXPECT_FALSE(read_result.ok());
  EXPECT_EQ(SOCKET_ERROR_CONNRESET, read_result.err_->getSystemErrorCode());

  Buffer::OwnedImpl out("hello");
  auto write_result = handle->write(out);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(SOCKET_ERROR_CONNRESET, write_result.err_->getSystemErrorCode());

  auto shutdown_result = handle->shutdown(SHUT_WR);
  EXPECT_EQ(-1, shutdown_result.return_value_);
  EXPECT_EQ(SOCKET_ERROR_CONNRESET, shutdown_result.errno_);

  setSwitch("setSocketInterfaceTestFailIo", 0);
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, WriteThatTakesNothingWouldBlock) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  // A zero-byte success counts as Again, or the raw buffer socket would retry it forever.
  for (int mode : {1, 3}) {
    setSwitch("setSocketInterfaceTestByteReport", mode);
    Buffer::OwnedImpl out("hello");
    EXPECT_TRUE(handle->write(out).wouldBlock());
    EXPECT_EQ(5, out.length());
    EXPECT_TRUE(handle->send("x", 1).wouldBlock());
  }
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, EmptyReadsAndWritesSkipTheModule) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  // The module would fail every call.
  setSwitch("setSocketInterfaceTestFailIo", 1);
  char byte;
  Buffer::RawSlice slice{&byte, 1};
  EXPECT_TRUE(handle->readv(0, &slice, 1).ok());
  EXPECT_TRUE(handle->recv(&byte, 0, 0).ok());
  Buffer::RawSlice empty{nullptr, 0};
  EXPECT_TRUE(handle->writev(&empty, 1).ok());
  setSwitch("setSocketInterfaceTestFailIo", 0);
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, OverReportedCountsAreClamped) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  setSwitch("setSocketInterfaceTestByteReport", 2);

  // An over-report would otherwise drain bytes never sent. Debug builds abort at the bug, so the
  // checks after it run in release builds only.
  Buffer::OwnedImpl out("hello");
  EXPECT_ENVOY_BUG(
      {
        auto result = handle->write(out);
        EXPECT_EQ(5, result.return_value_);
        EXPECT_EQ(0, out.length());
      },
      "Module wrote 10 bytes but was only offered 5");

  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");
  ASSERT_EQ(2, ::send(peer_fd, "ab", 2, 0));
  char buf[4];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  EXPECT_ENVOY_BUG(
      {
        auto result = handle->readv(sizeof(buf), &slice, 1);
        EXPECT_EQ(4, result.return_value_);
      },
      "Module read 7 bytes but only offered capacity for 4");
  setSwitch("setSocketInterfaceTestByteReport", 0);
  EXPECT_TRUE(handle->close().ok());
}

// =============================================================================
// Readiness
// =============================================================================

TEST_F(DynamicModuleSocketInterfaceTest, ReadinessReportedBeforeRegistrationIsDelivered) {
  installInterface("socket_interface_test");
  // The module reports the greeting from set_socket_handle, before Envoy registers the socket.
  moduleFunction<void (*)(const char*)>("setSocketInterfaceTestGreeting")("hi");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);

  registerHandle(*handle, Event::FileReadyType::Read | Event::FileReadyType::Write);
  // The kernel's Write completes the connect, and the held Read follows it.
  EXPECT_EQ(Event::FileReadyType::Read | Event::FileReadyType::Write, runAndTakeEvents());
  char buf[4];
  Buffer::RawSlice slice{buf, sizeof(buf)};
  auto result = handle->readv(sizeof(buf), &slice, 1);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("hi", std::string(buf, result.return_value_));
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, ReadinessWaitsForTheConnect) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  // Without Write interest the kernel never reports the connect done, so module readiness is
  // held: a Read before then would reach a connection that is still connecting.
  registerHandle(*handle, Event::FileReadyType::Read);
  moduleFunction<void (*)(uint32_t)>("triggerSocketInterfaceTestReady")(ModuleRead | ModuleWrite);
  EXPECT_EQ(0, runAndTakeEvents());

  handle->enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write);
  EXPECT_EQ(Event::FileReadyType::Read | Event::FileReadyType::Write, runAndTakeEvents());
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, ReadinessSurvivesDisabledEvents) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  completeConnect(*handle);

  // As readDisable() does, Envoy stops wanting reads while the module reports one.
  handle->enableFileEvents(Event::FileReadyType::Write);
  moduleFunction<void (*)(uint32_t)>("triggerSocketInterfaceTestReady")(ModuleRead);
  EXPECT_EQ(0, runAndTakeEvents() & Event::FileReadyType::Read);

  handle->enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write);
  EXPECT_TRUE(runAndTakeEvents() & Event::FileReadyType::Read);
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, ReadinessFromAnotherThreadIsDelivered) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  completeConnect(*handle);

  auto trigger = moduleFunction<void (*)(uint32_t)>("triggerSocketInterfaceTestReady");
  std::thread module_thread([trigger]() { trigger(ModuleRead); });
  module_thread.join();
  EXPECT_EQ(Event::FileReadyType::Read, runAndTakeEvents());
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, StaleDeliveryIsDroppedAndReplayedOnTheNextRegistration) {
  installInterface("socket_interface_test");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  completeConnect(*handle);

  // The delivery is posted for this registration, which ends before it runs.
  moduleFunction<void (*)(uint32_t)>("triggerSocketInterfaceTestReady")(ModuleRead);
  handle->resetFileEvents();
  EXPECT_EQ(0, runAndTakeEvents());

  // The pending Read reaches the next registration once its connect completes.
  registerHandle(*handle, Event::FileReadyType::Read | Event::FileReadyType::Write);
  EXPECT_TRUE(runAndTakeEvents() & Event::FileReadyType::Read);
  EXPECT_TRUE(handle->close().ok());
}

// =============================================================================
// Socket creation
// =============================================================================

TEST_F(DynamicModuleSocketInterfaceTest, CreationOnlyModuleUsesStandardHandle) {
  installInterface("socket_interface_creation_only");

  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  // The module's custom creation step.
  int nodelay = 0;
  socklen_t nodelay_len = sizeof(nodelay);
  ASSERT_EQ(0, handle->getOption(IPPROTO_TCP, TCP_NODELAY, &nodelay, &nodelay_len).return_value_);
  EXPECT_EQ(1, nodelay);

  // The module declines IPv6, which stays as available as the host makes it.
  EXPECT_TRUE(interface_.ipFamilySupported(AF_INET));
  EXPECT_EQ(Network::SocketInterfaceImpl().ipFamilySupported(AF_INET6),
            interface_.ipFamilySupported(AF_INET6));
  // The standard interface creates the IPv6 socket the module declines.
  auto v6_handle =
      interface_.socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                        Network::Address::IpVersion::v6, false, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, v6_handle);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(v6_handle.get()));
  EXPECT_TRUE(handle->close().ok());
  EXPECT_TRUE(v6_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, UseDefaultFallsBackToTheStandardInterface) {
  installInterface("socket_interface_creation_only", "default");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  // A standard socket binds normally.
  EXPECT_EQ(0,
            handle->bind(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0)).errno_);
  EXPECT_TRUE(handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, ErrorResultIsAFailingSocket) {
  installInterface("socket_interface_creation_only", "error");
  // A null handle would crash a listener, an upstream or statsd.
  for (auto socket_type : {Network::Socket::Type::Stream, Network::Socket::Type::Datagram}) {
    auto handle = makeSocket(1000, socket_type);
    ASSERT_NE(nullptr, handle);
    expectFailedSocket(*handle);
  }
  auto handle =
      interface_.socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                        Network::Address::IpVersion::v4, false, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, handle);
  expectFailedSocket(*handle);
  EXPECT_EQ(3, counter("socket_failed"));
}

TEST_F(DynamicModuleSocketInterfaceTest, FlagWithoutHooksIsRejected) {
  installInterface("socket_interface_creation_only", "flag_without_hooks");
  const int initial_close_count = moduleCounter("getSocketInterfaceCreationOnlyCloseCount");
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  expectFailedSocket(*handle);
  // The rejected socket's state is released.
  EXPECT_EQ(initial_close_count + 1, moduleCounter("getSocketInterfaceCreationOnlyCloseCount"));
  EXPECT_EQ(1, counter("socket_failed"));
}

TEST_F(DynamicModuleSocketInterfaceTest, WrongFamilyIsRejected) {
  installInterface("socket_interface_creation_only", "wrong_family");
  // An IPv6 socket on an IPv4 listener or upstream would fail deep in Envoy instead.
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  expectFailedSocket(*handle);
  EXPECT_EQ(1, counter("socket_failed"));
}

TEST_F(DynamicModuleSocketInterfaceTest, UnusableCreatedSocketsAreRejected) {
  installInterface("socket_interface_test");
  const int initial_close_count = moduleCounter("getSocketInterfaceTestCloseCount");
  // Both flags, HandsOverConnections on a connected socket, and a flag without a per-socket state.
  for (int mode : {1, 2, 3}) {
    setSwitch("setSocketInterfaceTestCreateFlagsMode", mode);
    auto handle = makeSocket(1000);
    ASSERT_NE(nullptr, handle);
    expectFailedSocket(*handle);
  }
  setSwitch("setSocketInterfaceTestCreateFlagsMode", 0);
  // An invalid file descriptor.
  setSwitch("setSocketInterfaceTestForceBadFd", 1);
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  expectFailedSocket(*handle);
  EXPECT_EQ(4, counter("socket_failed"));
  // The per-socket states of the rejected sockets are released.
  EXPECT_EQ(initial_close_count + 3, moduleCounter("getSocketInterfaceTestCloseCount"));
}

TEST_F(DynamicModuleSocketInterfaceTest, FlagsRequireHotRestartDisabled) {
  context_.options_.hot_restart_disabled_ = false;
  installInterface("socket_interface_test");
  // A hot-restarted child would adopt the socket without its module state.
  auto handle = makeSocket(1000);
  ASSERT_NE(nullptr, handle);
  expectFailedSocket(*handle);
  EXPECT_EQ(1, counter("socket_failed"));
}

TEST_F(DynamicModuleSocketInterfaceTest, RecordsCreationArguments) {
  installInterface("socket_interface_test");
  auto last_address = [this]() {
    return std::string(
        moduleFunction<const char* (*)(void)>("getSocketInterfaceTestLastAddress")());
  };

  // The module declines IPv6, so the standard interface creates the socket, but the module saw
  // the request with the IPv6 arguments.
  auto v6_addr = std::make_shared<Network::Address::Ipv6Instance>("::1", 1000, nullptr, true);
  auto v6_handle =
      interface_.socket(Network::Socket::Type::Stream, v6_addr, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, v6_handle);
  EXPECT_EQ("::1", last_address());
  EXPECT_EQ(1000, moduleFunction<uint32_t (*)(void)>("getSocketInterfaceTestLastPort")());
  EXPECT_EQ(AF_INET6, moduleCounter("getSocketInterfaceTestLastDomain"));
  EXPECT_EQ(1, moduleCounter("getSocketInterfaceTestLastV6only"));
  EXPECT_TRUE(v6_handle->close().ok());

  Network::Address::InstanceConstSharedPtr pipe_addr =
      *Network::Address::PipeInstance::create("/tmp/dynamic_module_socket_interface_test.sock");
  auto pipe_handle =
      interface_.socket(Network::Socket::Type::Stream, pipe_addr, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, pipe_handle);
  EXPECT_EQ("/tmp/dynamic_module_socket_interface_test.sock", last_address());
  EXPECT_EQ(0, moduleFunction<uint32_t (*)(void)>("getSocketInterfaceTestLastPort")());
  EXPECT_EQ(AF_UNIX, moduleCounter("getSocketInterfaceTestLastDomain"));
  EXPECT_EQ(0, moduleCounter("getSocketInterfaceTestLastV6only"));
  EXPECT_TRUE(pipe_handle->close().ok());

#if defined(__linux__)
  // An abstract socket's name starts with a NUL byte, which reaches the module as "@".
  Network::Address::InstanceConstSharedPtr abstract_addr =
      *Network::Address::PipeInstance::create("@dynamic_module_socket_interface_test");
  auto abstract_handle = interface_.socket(Network::Socket::Type::Stream, abstract_addr,
                                           Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, abstract_handle);
  EXPECT_EQ("@dynamic_module_socket_interface_test", last_address());
  EXPECT_TRUE(abstract_handle->close().ok());
#endif
}

TEST_F(DynamicModuleSocketInterfaceTest, AddrTypeOverloadDelegatesToModule) {
  installInterface("socket_interface_test");
  auto handle =
      interface_.socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                        Network::Address::IpVersion::v4, false, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  // A delegated socket that does not hand over connections accepts on its file descriptor,
  // which is not listening.
  sockaddr_storage addr = {};
  socklen_t addr_len = sizeof(addr);
  EXPECT_EQ(nullptr, handle->accept(reinterpret_cast<sockaddr*>(&addr), &addr_len));
  EXPECT_TRUE(handle->close().ok());
}

// =============================================================================
// Listen sockets that hand over connections
// =============================================================================

TEST_F(DynamicModuleSocketInterfaceTest, HandsOverConnectionsOnReportedReadiness) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(listen_handle.get()));
  // The socket never takes the listener's address or listens.
  EXPECT_EQ(0, listen_handle->bind(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0))
                   .return_value_);
  EXPECT_EQ(0, listen_handle->listen(128).return_value_);

  // Kernel interest stays off, so nothing fires until the module reports a connection, which it
  // does from another thread.
  registerHandle(*listen_handle, Event::FileReadyType::Read, Event::FileTriggerType::Level);
  EXPECT_EQ(0, runAndTakeEvents());
  auto queue = moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections");
  std::thread module_thread([queue]() { queue(1); });
  module_thread.join();
  EXPECT_EQ(Event::FileReadyType::Read, runAndTakeEvents());

  const int initial_accept_count = moduleCounter("getSocketInterfaceTestAcceptCount");
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  auto accepted = listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(accepted.get()));
  EXPECT_EQ(initial_accept_count + 1, moduleCounter("getSocketInterfaceTestAcceptCount"));
  // The peer address comes from the connected socket the module handed over.
  EXPECT_EQ(AF_INET, remote_addr.ss_family);

  // The accepted connection delegates its I/O.
  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");
  ASSERT_EQ(3, ::send(peer_fd, "abc", 3, 0));
  char buf[4] = {0};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  auto read_result = accepted->readv(sizeof(buf), &slice, 1);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ("abc", std::string(buf, read_result.return_value_));

  // No connection is pending, and with the readiness consumed nothing fires again.
  EXPECT_EQ(nullptr, listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len));
  EXPECT_EQ(0, runAndTakeEvents());
  EXPECT_TRUE(accepted->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, AcceptReportsTheModuleAddresses) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  setSwitch("setSocketInterfaceTestReportAcceptAddresses", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, listen_handle);
  moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);

  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  auto accepted = listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, accepted);
  auto remote = Network::Address::addressFromSockAddr(remote_addr, remote_len);
  ASSERT_TRUE(remote.ok());
  EXPECT_EQ("10.9.8.7:1234", (*remote)->asString());
  // The local address drives filter chain matching on a wildcard listener.
  EXPECT_EQ("10.1.2.3:443", (*accepted->localAddress())->asString());
  EXPECT_EQ("10.9.8.7:1234", (*accepted->peerAddress())->asString());
  EXPECT_TRUE(accepted->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, PausedListenerDefersTheAccept) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, listen_handle);
  registerHandle(*listen_handle, Event::FileReadyType::Read, Event::FileTriggerType::Level);

  // As the overload manager does, Envoy pauses the listener before the module reports.
  listen_handle->enableFileEvents(0);
  moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);
  EXPECT_EQ(0, runAndTakeEvents());

  listen_handle->enableFileEvents(Event::FileReadyType::Read);
  EXPECT_EQ(Event::FileReadyType::Read, runAndTakeEvents());
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  auto accepted = listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, accepted);
  EXPECT_TRUE(accepted->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, InvalidHandedOverConnectionsAreRejected) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, listen_handle);
  const int initial_close_count = moduleCounter("getSocketInterfaceTestCloseCount");
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  // An invalid flag, DelegateIo without a per-socket state, an unconnected socket, an invalid
  // port, and an address of another family.
  for (int mode : {2, 3, 4, 5, 6}) {
    setSwitch("setSocketInterfaceTestAcceptMode", mode);
    moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);
    EXPECT_EQ(nullptr,
              listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len));
  }
  setSwitch("setSocketInterfaceTestAcceptMode", 0);
  // An invalid file descriptor, and a failing accept hook.
  setSwitch("setSocketInterfaceTestForceBadFd", 1);
  moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);
  EXPECT_EQ(nullptr, listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len));
  setSwitch("setSocketInterfaceTestForceBadFd", 0);
  setSwitch("setSocketInterfaceTestFailIo", 1);
  EXPECT_EQ(nullptr, listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len));
  setSwitch("setSocketInterfaceTestFailIo", 0);
  EXPECT_EQ(7, counter("accept_failed"));
  // The per-socket states handed over with modes 2, 5 and 6 are released.
  EXPECT_EQ(initial_close_count + 3, moduleCounter("getSocketInterfaceTestCloseCount"));
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, HandedOverConnectionWithoutFlagsUsesTheKernel) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  setSwitch("setSocketInterfaceTestAcceptMode", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, listen_handle);
  moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  auto accepted = listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(accepted.get()));
  const int peer_fd = moduleCounter("getSocketInterfaceTestPeerFd");

  // The kernel signals the connection's readiness and moves its bytes, even with the module
  // failing every I/O hook.
  setSwitch("setSocketInterfaceTestFailIo", 1);
  registerHandle(*accepted, 0);
  accepted->enableFileEvents(Event::FileReadyType::Read);
  ASSERT_EQ(3, ::send(peer_fd, "abc", 3, 0));
  EXPECT_EQ(Event::FileReadyType::Read, runAndTakeEvents());
  char buf[4] = {0};
  ASSERT_EQ(1, accepted->recv(buf, 1, MSG_PEEK).return_value_);
  Buffer::RawSlice slice{buf, sizeof(buf)};
  auto read_result = accepted->readv(sizeof(buf), &slice, 1);
  ASSERT_TRUE(read_result.ok());
  EXPECT_EQ("abc", std::string(buf, read_result.return_value_));
  Buffer::OwnedImpl out("xy");
  ASSERT_EQ(2, accepted->write(out).return_value_);
  ASSERT_EQ(1, accepted->send("z", 1).return_value_);
  EXPECT_EQ(0, accepted->shutdown(SHUT_WR).return_value_);
  ASSERT_EQ(3, ::recv(peer_fd, buf, sizeof(buf), 0));
  EXPECT_EQ("xyz", std::string(buf, 3));
  setSwitch("setSocketInterfaceTestFailIo", 0);

  // Without reported addresses, the addresses are the connected socket's own.
  EXPECT_EQ(AF_INET, remote_addr.ss_family);
  EXPECT_EQ("127.0.0.1", (*accepted->peerAddress())->ip()->addressAsString());
  auto copy = accepted->duplicate();
  ASSERT_NE(nullptr, copy);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(copy.get()));
  EXPECT_TRUE(copy->close().ok());
  EXPECT_TRUE(accepted->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, UnixListenSocketHandsOverConnections) {
  installInterface("socket_interface_test");
  // Outside listen mode the module leaves AF_UNIX sockets to the standard interface.
  auto standard =
      interface_.socket(Network::Socket::Type::Stream, Network::Address::Type::Pipe,
                        Network::Address::IpVersion::v4, false, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, standard);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(standard.get()));
  EXPECT_TRUE(standard->close().ok());

  setSwitch("setSocketInterfaceTestListenMode", 1);
  auto listen_address = Network::Address::PipeInstance::create("/tmp/socket_interface_test_listen");
  ASSERT_TRUE(listen_address.ok());
  auto listen_handle =
      interface_.socket(Network::Socket::Type::Stream, std::move(listen_address.value()),
                        Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(listen_handle.get()));
  auto queue = moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections");
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);

  // Envoy would name the peer of an unnamed pair after the pair itself.
  queue(1);
  EXPECT_EQ(nullptr, listen_handle->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len));
  EXPECT_EQ(1, counter("accept_failed"));

  // The copy a worker accepts on stays an AF_UNIX listen socket.
  auto copy = listen_handle->duplicate();
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(copy.get()));
  setSwitch("setSocketInterfaceTestReportAcceptAddresses", 1);
  queue(1);
  auto accepted = copy->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, accepted);
  EXPECT_EQ(AF_UNIX, remote_addr.ss_family);
  EXPECT_EQ("/tmp/socket_interface_test_local", (*accepted->localAddress())->asString());
  EXPECT_EQ("/tmp/socket_interface_test_peer", (*accepted->peerAddress())->asString());
  EXPECT_TRUE(accepted->close().ok());
  EXPECT_TRUE(copy->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST_F(DynamicModuleSocketInterfaceTest, DuplicateCopiesTheModuleState) {
  installInterface("socket_interface_test");
  setSwitch("setSocketInterfaceTestListenMode", 1);
  auto listen_handle = makeSocket(0);
  ASSERT_NE(nullptr, listen_handle);

  // Envoy copies a listen socket for each worker when the listener does not use reuse_port.
  const int initial_duplicate_count = moduleCounter("getSocketInterfaceTestDuplicateCount");
  auto copy = listen_handle->duplicate();
  ASSERT_NE(nullptr, dynamic_cast<DynamicModuleIoHandle*>(copy.get()));
  EXPECT_EQ(initial_duplicate_count + 1, moduleCounter("getSocketInterfaceTestDuplicateCount"));
  // The copy keeps handing over connections with its own state and readiness.
  registerHandle(*copy, Event::FileReadyType::Read, Event::FileTriggerType::Level);
  moduleFunction<void (*)(int)>("queueSocketInterfaceTestConnections")(1);
  EXPECT_EQ(Event::FileReadyType::Read, runAndTakeEvents());
  sockaddr_storage remote_addr = {};
  socklen_t remote_len = sizeof(remote_addr);
  auto accepted = copy->accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_len);
  ASSERT_NE(nullptr, accepted);
  EXPECT_TRUE(accepted->close().ok());

  // A copy the module fails to make is a socket whose operations fail.
  setSwitch("setSocketInterfaceTestFailIo", 1);
  auto failed_copy = listen_handle->duplicate();
  ASSERT_NE(nullptr, failed_copy);
  expectFailedSocket(*failed_copy);
  setSwitch("setSocketInterfaceTestFailIo", 0);
  // So is a copy of another family, whose file descriptor and state are released.
  setSwitch("setSocketInterfaceTestForceBadFd", 1);
  const int initial_close_count = moduleCounter("getSocketInterfaceTestCloseCount");
  auto wrong_family_copy = listen_handle->duplicate();
  ASSERT_NE(nullptr, wrong_family_copy);
  expectFailedSocket(*wrong_family_copy);
  EXPECT_EQ(initial_close_count + 1, moduleCounter("getSocketInterfaceTestCloseCount"));
  setSwitch("setSocketInterfaceTestForceBadFd", 0);
  EXPECT_EQ(2, counter("duplicate_failed"));
  EXPECT_TRUE(copy->close().ok());
  EXPECT_TRUE(listen_handle->close().ok());
}

TEST(DynamicModuleFailedSocketTest, SocketFailureIsABug) {
  testing::NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  ON_CALL(os_sys_calls, socket(AF_INET, SOCK_STREAM, 0))
      .WillByDefault(testing::Return(Api::SysCallSocketResult{INVALID_SOCKET, EMFILE}));
  EXPECT_ENVOY_BUG(
      EXPECT_EQ(nullptr, makeFailedIoHandle(AF_INET, Network::Socket::Type::Stream, false)),
      "socket(2) failed");
}

TEST(DynamicModuleSocketInterfaceFallbackTest, WithoutExtensionUsesStandardHandle) {
  DynamicModuleSocketInterface interface;
  auto addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1000);
  auto handle =
      interface.socket(Network::Socket::Type::Stream, addr, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, handle);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(handle.get()));
  EXPECT_TRUE(handle->close().ok());

  auto addr_type_handle =
      interface.socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                       Network::Address::IpVersion::v4, false, Network::SocketCreationOptions{});
  ASSERT_NE(nullptr, addr_type_handle);
  EXPECT_EQ(nullptr, dynamic_cast<DynamicModuleIoHandle*>(addr_type_handle.get()));
  EXPECT_TRUE(addr_type_handle->close().ok());
  EXPECT_EQ(Network::SocketInterfaceImpl().ipFamilySupported(AF_INET6),
            interface.ipFamilySupported(AF_INET6));
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
