#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "source/common/network/transport_socket_options_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/network/connection.h"

using testing::Return;

namespace Envoy {
namespace Network {

class HappyEyeballsConnectionImplTest : public testing::Test {
 public:
  HappyEyeballsConnectionImplTest()
      : transport_socket_options_(std::make_shared<TransportSocketOptionsImpl>()),
        options_(std::make_shared<ConnectionSocket::Options>()) {
    EXPECT_CALL(transport_socket_factory_, createTransportSocket(_));
    connection_ = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
    impl_ = std::make_unique<HappyEyeballsConnectionImpl>(dispatcher_,
                                                          Network::Address::InstanceConstSharedPtr(),
                                                          Network::Address::InstanceConstSharedPtr(),
                                                          transport_socket_factory_,
                                                          transport_socket_options_,
                                                          options_);


  }

 protected:
  Event::MockDispatcher dispatcher_;
  MockTransportSocketFactory transport_socket_factory_;
  TransportSocketOptionsSharedPtr transport_socket_options_;
  const ConnectionSocket::OptionsSharedPtr options_;
  Network::MockClientConnection* connection_;
  std::unique_ptr<HappyEyeballsConnectionImpl> impl_;
};

TEST_F(HappyEyeballsConnectionImplTest, Connect) {
  EXPECT_CALL(*connection_, connect());
  impl_->connect();
};

} // namespace Network
} // namespace Envoy
