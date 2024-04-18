#include <memory>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/listener_manager/lds_api.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Throw;

namespace Envoy {
namespace Server {
namespace {

class LdsApiTest : public testing::Test {
public:
  LdsApiTest() {
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
  }

  void setup() {
    envoy::config::core::v3::ConfigSource lds_config;
    EXPECT_CALL(init_manager_, add(_));
    lds_ =
        std::make_unique<LdsApiImpl>(lds_config, nullptr, cluster_manager_, init_manager_,
                                     *store_.rootScope(), listener_manager_, validation_visitor_);
    EXPECT_CALL(*cluster_manager_.subscription_factory_.subscription_, start(_));
    init_target_handle_->initialize(init_watcher_);
    lds_callbacks_ = cluster_manager_.subscription_factory_.callbacks_;
  }

  void expectAdd(const std::string& listener_name, absl::optional<std::string> version,
                 bool updated) {
    if (!version) {
      EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
          .WillOnce(
              Invoke([listener_name, updated](const envoy::config::listener::v3::Listener& config,
                                              const std::string&, bool) -> bool {
                EXPECT_EQ(listener_name, config.name());
                return updated;
              }));
    } else {
      EXPECT_CALL(listener_manager_, addOrUpdateListener(_, version.value(), true))
          .WillOnce(
              Invoke([listener_name, updated](const envoy::config::listener::v3::Listener& config,
                                              const std::string&, bool) -> bool {
                EXPECT_EQ(listener_name, config.name());
                return updated;
              }));
    }
  }

  void makeListenersAndExpectCall(const std::vector<std::string>& listener_names) {
    std::vector<std::reference_wrapper<Network::ListenerConfig>> refs;
    listeners_.clear();
    for (const auto& name : listener_names) {
      listeners_.emplace_back();
      listeners_.back().name_ = name;
      refs.emplace_back(listeners_.back());
    }
    EXPECT_CALL(listener_manager_, listeners(ListenerManager::WARMING | ListenerManager::ACTIVE))
        .WillOnce(Return(refs));
    EXPECT_CALL(listener_manager_, beginListenerUpdate());
  }

  envoy::config::listener::v3::Listener buildListener(const std::string& listener_name) {
    envoy::config::listener::v3::Listener listener;
    listener.set_name(listener_name);
    auto socket_address = listener.mutable_address()->mutable_socket_address();
    socket_address->set_address(listener_name);
    socket_address->set_port_value(1);
    listener.add_filter_chains();
    return listener;
  }

  std::shared_ptr<NiceMock<Config::MockGrpcMux>> grpc_mux_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Stats::IsolatedStoreImpl store_;
  MockListenerManager listener_manager_;
  Config::SubscriptionCallbacks* lds_callbacks_{};
  std::unique_ptr<LdsApiImpl> lds_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

private:
  std::list<NiceMock<Network::MockListenerConfig>> listeners_;
};

TEST_F(LdsApiTest, MisconfiguredListenerNameIsPresentInException) {
  InSequence s;

  setup();

  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  // Construct a minimal listener that would pass proto validation.
  envoy::config::listener::v3::Listener listener;
  listener.set_name("invalid-listener");
  auto socket_address = listener.mutable_address()->mutable_socket_address();
  socket_address->set_address("invalid-address");
  socket_address->set_port_value(1);
  listener.add_filter_chains();

  EXPECT_CALL(listener_manager_, listeners(ListenerManager::WARMING | ListenerManager::ACTIVE))
      .WillOnce(Return(existing_listeners));

  EXPECT_CALL(listener_manager_, beginListenerUpdate());
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Throw(EnvoyException("something is wrong")));
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());

  const auto decoded_resources = TestUtility::decodeResources({listener});
  EXPECT_EQ(lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
            "Error adding/updating listener(s) invalid-listener: something is wrong\n");
}

TEST_F(LdsApiTest, EmptyListenersUpdate) {
  InSequence s;

  setup();

  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  EXPECT_CALL(listener_manager_, listeners(ListenerManager::WARMING | ListenerManager::ACTIVE))
      .WillOnce(Return(existing_listeners));
  EXPECT_CALL(listener_manager_, beginListenerUpdate());
  EXPECT_CALL(listener_manager_, endListenerUpdate(_))
      .WillOnce(Invoke([](ListenerManager::FailureStates&& state) { EXPECT_EQ(0, state.size()); }));
  ;
  EXPECT_CALL(init_watcher_, ready());

  EXPECT_TRUE(lds_callbacks_->onConfigUpdate({}, "").ok());
}

TEST_F(LdsApiTest, ListenerCreationContinuesEvenAfterException) {
  InSequence s;

  setup();

  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  // Add 4 listeners - 2 valid and 2 invalid.
  const auto listener_0 = buildListener("valid-listener-1");
  const auto listener_1 = buildListener("invalid-listener-1");
  const auto listener_2 = buildListener("valid-listener-2");
  const auto listener_3 = buildListener("invalid-listener-2");

  EXPECT_CALL(listener_manager_, listeners(ListenerManager::WARMING | ListenerManager::ACTIVE))
      .WillOnce(Return(existing_listeners));

  EXPECT_CALL(listener_manager_, beginListenerUpdate());
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Return(true))
      .WillOnce(Throw(EnvoyException("something is wrong")))
      .WillOnce(Return(true))
      .WillOnce(Throw(EnvoyException("something else is wrong")));
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));

  EXPECT_CALL(init_watcher_, ready());

  const auto decoded_resources =
      TestUtility::decodeResources({listener_0, listener_1, listener_2, listener_3});
  EXPECT_EQ(lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
            "Error adding/updating listener(s) invalid-listener-1: something is "
            "wrong\ninvalid-listener-2: something else is wrong\n");
}

// Validate onConfigUpdate throws EnvoyException with duplicate listeners.
// The first of the duplicates will be successfully applied, with the rest adding to
// the exception message.
TEST_F(LdsApiTest, ValidateDuplicateListeners) {
  InSequence s;

  setup();

  const auto listener = buildListener("duplicate_listener");

  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;
  EXPECT_CALL(listener_manager_, listeners(ListenerManager::WARMING | ListenerManager::ACTIVE))
      .WillOnce(Return(existing_listeners));
  EXPECT_CALL(listener_manager_, beginListenerUpdate());
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true)).WillOnce(Return(true));
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());

  const auto decoded_resources = TestUtility::decodeResources({listener, listener});
  EXPECT_EQ(lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
            "Error adding/updating listener(s) duplicate_listener: duplicate "
            "listener duplicate_listener found\n");
}

TEST_F(LdsApiTest, Basic) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener2",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);

  makeListenersAndExpectCall({});
  expectAdd("listener1", "0", true);
  expectAdd("listener2", "0", true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response1);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());

  EXPECT_EQ("0", lds_->versionInfo());

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener3",
      "address": { "socket_address": { "address": "tcp://0.0.0.3", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
  )EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_json);

  makeListenersAndExpectCall({"listener1", "listener2"});
  EXPECT_CALL(listener_manager_, removeListener("listener2")).WillOnce(Return(true));
  expectAdd("listener1", "1", false);
  expectAdd("listener3", "1", true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response2);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok());
  EXPECT_EQ("1", lds_->versionInfo());
}

// Regression test against only updating versionInfo() if at least one listener
// is added/updated even if one or more are removed.
TEST_F(LdsApiTest, UpdateVersionOnListenerRemove) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);

  makeListenersAndExpectCall({});
  expectAdd("listener1", "0", true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response1);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());

  EXPECT_EQ("0", lds_->versionInfo());

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": []
}
  )EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_json);

  makeListenersAndExpectCall({"listener1"});
  EXPECT_CALL(listener_manager_, removeListener("listener1")).WillOnce(Return(true));
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response2);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok());
  EXPECT_EQ("1", lds_->versionInfo());
}

// Regression test issue #2188 where an empty ca_cert_file field was created and caused the LDS
// update to fail validation.
TEST_F(LdsApiTest, TlsConfigWithoutCaCert) {
  InSequence s;

  setup();

  std::string response1_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.config.listener.v3.Listener
  name: listener0
  address:
    socket_address:
      address: tcp://0.0.0.1
      port_value: 61000
  filter_chains:
  - filters:
  )EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);

  makeListenersAndExpectCall({"listener0"});
  expectAdd("listener0", {}, true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response1);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());

  constexpr absl::string_view response2_basic = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.config.listener.v3.Listener
  name: listener-8080
  address:
    socket_address:
      address: tcp://0.0.0.0
      port_value: 61001
  filter_chains:
  - transport_socket:
      name: tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{}"
            private_key:
              filename: "{}"
    filters:
  )EOF";
  std::string response2_json =
      fmt::format(response2_basic,
                  TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
                  TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_json);

  makeListenersAndExpectCall({
      "listener-8080",
  });
  // Can't check version here because of bazel sandbox paths for the certs.
  expectAdd("listener-8080", {}, true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response2);
  EXPECT_NO_THROW(EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok()));
}

// Validate behavior when the config fails delivery at the subscription level.
TEST_F(LdsApiTest, FailureSubscription) {
  InSequence s;

  setup();

  EXPECT_CALL(init_watcher_, ready());
  lds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, {});
  EXPECT_EQ("", lds_->versionInfo());
}

TEST_F(LdsApiTest, ReplacingListenerWithSameAddress) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener2",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_json);

  makeListenersAndExpectCall({});
  expectAdd("listener1", "0", true);
  expectAdd("listener2", "0", true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response1);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());

  EXPECT_EQ("0", lds_->versionInfo());

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.config.listener.v3.Listener",
      "name": "listener3",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_json);

  makeListenersAndExpectCall({"listener1", "listener2"});
  EXPECT_CALL(listener_manager_, removeListener("listener2")).WillOnce(Return(true));
  expectAdd("listener1", "1", false);
  expectAdd("listener3", "1", true);
  EXPECT_CALL(listener_manager_, endListenerUpdate(_));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::listener::v3::Listener>(response2);
  EXPECT_TRUE(
      lds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok());
}

} // namespace
} // namespace Server
} // namespace Envoy
