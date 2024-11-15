#include "test/server/admin/admin_instance.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test Using /init_dump to dump information of all unready targets.
TEST_P(AdminInstanceTest, UnreadyTargetsDump) {
  Buffer::OwnedImpl response;
  Buffer::OwnedImpl response2;
  Http::TestResponseHeaderMapImpl header_map;

  Network::MockListenerConfig listener_1;
  Init::ManagerImpl init_manager_1{"test_init_manager_1"};
  Init::TargetImpl target_1("test_target_1", nullptr);
  init_manager_1.add(target_1);
  EXPECT_CALL(listener_1, initManager()).WillRepeatedly(ReturnRef(init_manager_1));

  Network::MockListenerConfig listener_2;
  Init::ManagerImpl init_manager_2{"test_init_manager_2"};
  Init::TargetImpl target_2("test_target_2", nullptr);
  init_manager_2.add(target_2);
  EXPECT_CALL(listener_2, initManager()).WillRepeatedly(ReturnRef(init_manager_2));

  MockListenerManager listener_manager;
  EXPECT_CALL(server_, listenerManager()).WillRepeatedly(ReturnRef(listener_manager));

  std::vector<std::reference_wrapper<Envoy::Network::ListenerConfig>> listeners;
  listeners.push_back(listener_1);
  listeners.push_back(listener_2);
  EXPECT_CALL(listener_manager, isWorkerStarted()).WillRepeatedly(Return(true));
  EXPECT_CALL(listener_manager, listeners(_)).WillRepeatedly(Return(listeners));

  // Perform test twice - once with no params and once with empty params
  // The second mirrors the behavior of call from the admin landing page
  EXPECT_EQ(Http::Code::OK, getCallback("/init_dump", header_map, response));
  EXPECT_EQ(Http::Code::OK, getCallback("/init_dump?mask=", header_map, response2));
  // The expected value should be updated when ProtobufTypes::MessagePtr
  // AdminImpl::dumpListenerUnreadyTargets function includes more dump when mask has no value.
  const std::string expected_json = R"EOF({
 "unready_targets_dumps": [
  {
   "name": "init manager test_init_manager_1",
   "target_names": [
    "target test_target_1"
   ]
  },
  {
   "name": "init manager test_init_manager_2",
   "target_names": [
    "target test_target_2"
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(response.toString(), expected_json);
  EXPECT_EQ(response2.toString(), expected_json);
}

// Test Using /init_dump?listener to dump unready targets of listeners.
TEST_P(AdminInstanceTest, ListenerUnreadyTargetsDump) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;

  Network::MockListenerConfig listener_1;
  Init::ManagerImpl init_manager_1{"test_init_manager_1"};
  Init::TargetImpl target_1("test_target_1", nullptr);
  init_manager_1.add(target_1);
  EXPECT_CALL(listener_1, initManager()).WillOnce(ReturnRef(init_manager_1));

  Network::MockListenerConfig listener_2;
  Init::ManagerImpl init_manager_2{"test_init_manager_2"};
  Init::TargetImpl target_2("test_target_2", nullptr);
  init_manager_2.add(target_2);
  EXPECT_CALL(listener_2, initManager()).WillOnce(ReturnRef(init_manager_2));

  MockListenerManager listener_manager;
  EXPECT_CALL(server_, listenerManager()).WillRepeatedly(ReturnRef(listener_manager));

  std::vector<std::reference_wrapper<Envoy::Network::ListenerConfig>> listeners;
  listeners.push_back(listener_1);
  listeners.push_back(listener_2);
  EXPECT_CALL(listener_manager, isWorkerStarted()).WillRepeatedly(Return(true));
  EXPECT_CALL(listener_manager, listeners(_)).WillOnce(Return(listeners));

  EXPECT_EQ(Http::Code::OK, getCallback("/init_dump?mask=listener", header_map, response));
  std::string output = response.toString();
  const std::string expected_json = R"EOF({
 "unready_targets_dumps": [
  {
   "name": "init manager test_init_manager_1",
   "target_names": [
    "target test_target_1"
   ]
  },
  {
   "name": "init manager test_init_manager_2",
   "target_names": [
    "target test_target_2"
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(output, expected_json);
}
} // namespace Server
} // namespace Envoy
