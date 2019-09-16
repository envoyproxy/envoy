#include "test/server/listener_manager_impl_test.h"
#include "test/test_common/threadsafe_singleton_injector.h"

using testing::AtLeast;

namespace Envoy {
namespace Server {
namespace {

class ListenerManagerImplQuicOnlyTest : public ListenerManagerImplTest {
protected:
  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
};

TEST_F(ListenerManagerImplQuicOnlyTest, QuicListenerFactory) {
  const std::string proto_text = R"EOF(
address: {
  socket_address: {
    protocol: UDP
    address: "127.0.0.1"
    port_value: 1234
  }
}
filter_chains: {}
udp_listener_config: {
  udp_listener_name: "quiche_quic_listener"
  config: {}
}
  )EOF";
  envoy::api::v2::Listener listener_proto;
  EXPECT_TRUE(Protobuf::TextFormat::ParseFromString(proto_text, &listener_proto));

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_,
              createListenSocket(_, Network::Address::SocketType::Datagram, _, true));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(os_sys_calls_, close(_)).WillRepeatedly(Return(Api::SysCallIntResult{0, errno}));
  manager_->addOrUpdateListener(listener_proto, "", true);
  EXPECT_EQ(1u, manager_->listeners().size());
  EXPECT_NE(nullptr, manager_->listeners()[0].get().udpListenerFactory());
}

} // namespace
} // namespace Server
} // namespace Envoy
