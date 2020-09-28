#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "source/common/network/_virtual_includes/address_lib/common/network/address_impl.h"
#include "source/common/network/_virtual_includes/connection_lib/common/network/connection_impl.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::StrictMock;

namespace Envoy {
namespace Network {

class ClosingClientConnectionImplTest : public testing::Test {
protected:
  ClosingClientConnectionImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  const std::shared_ptr<Address::EnvoyInternalInstance> remote_address_{
      std::make_shared<Address::EnvoyInternalInstance>("listener_addr")};
  const std::shared_ptr<Address::EnvoyInternalInstance> source_address_{
      std::make_shared<Address::EnvoyInternalInstance>("local_addr")};
};

TEST_F(ClosingClientConnectionImplTest, ActiveClose) {
  ClosingClientConnectionImpl conn(*dispatcher_, remote_address_, source_address_);
  conn.close(ConnectionCloseType::NoFlush);
  EXPECT_EQ(Connection::State::Closed, conn.state());
}

TEST_F(ClosingClientConnectionImplTest, PassiveCloseDriveByDispatcher) {
  MockConnectionCallbacks client_callbacks;
  ClosingClientConnectionImpl conn(*dispatcher_, remote_address_, source_address_);
  conn.addConnectionCallbacks(client_callbacks);
  EXPECT_EQ(Connection::State::Open, conn.state());

  EXPECT_CALL(client_callbacks, onEvent(Network::ConnectionEvent::RemoteClose)).Times(1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Connection::State::Closed, conn.state());
}

TEST_F(ClosingClientConnectionImplTest, ClosingConnectionCreatedByDispatcher) {
  auto conn = dispatcher_->createInternalConnection(
      std::make_shared<Address::EnvoyInternalInstance>("listener_id"),
      std::make_shared<Address::EnvoyInternalInstance>("client_id"));

  MockConnectionCallbacks client_callbacks;
  conn->addConnectionCallbacks(client_callbacks);
  EXPECT_EQ(Connection::State::Open, conn->state());

  EXPECT_CALL(client_callbacks, onEvent(Network::ConnectionEvent::RemoteClose)).Times(1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Connection::State::Closed, conn->state());
}

} // namespace Network
} // namespace Envoy
