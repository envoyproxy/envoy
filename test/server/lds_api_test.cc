#include <memory>

#include "envoy/api/v2/lds.pb.h"

#include "common/config/utility.h"
#include "common/http/message_impl.h"

#include "server/lds_api.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Server {
namespace {

class LdsApiTest : public testing::Test {
public:
  LdsApiTest() : request_(&cluster_manager_.async_client_), api_(Api::createApiForTest(store_)) {}

  void setup() {
    const std::string config_json = R"EOF(
    {
      "api_type": "REST",
      "cluster": "foo_cluster",
      "refresh_delay_ms": 1000
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
    envoy::api::v2::core::ConfigSource lds_config;
    Config::Utility::translateLdsConfig(*config, lds_config);
    lds_config.mutable_api_config_source()->set_api_type(
        envoy::api::v2::core::ApiConfigSource::REST);
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockClusterMockPrioritySet cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(cluster_manager_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, type());
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(init_, registerTarget(_, _));
    lds_ = std::make_unique<LdsApiImpl>(lds_config, cluster_manager_, dispatcher_, random_, init_,
                                        local_info_, store_, listener_manager_, *api_);

    expectRequest();
    init_.initialize();
  }

  void expectAdd(const std::string& listener_name, absl::optional<std::string> version,
                 bool updated) {
    if (!version) {
      EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
          .WillOnce(Invoke([listener_name, updated](const envoy::api::v2::Listener& config,
                                                    const std::string&, bool) -> bool {
            EXPECT_EQ(listener_name, config.name());
            return updated;
          }));
    } else {
      EXPECT_CALL(listener_manager_, addOrUpdateListener(_, version.value(), true))
          .WillOnce(Invoke([listener_name, updated](const envoy::api::v2::Listener& config,
                                                    const std::string&, bool) -> bool {
            EXPECT_EQ(listener_name, config.name());
            return updated;
          }));
    }
  }

  void expectRequest() {
    EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("foo_cluster"));
    EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              EXPECT_EQ(
                  (Http::TestHeaderMapImpl{
                      {":method", "POST"},
                      {":path", "/v2/discovery:listeners"},
                      {":authority", "foo_cluster"},
                      {"content-type", "application/json"},
                      {"content-length",
                       request->body() ? fmt::format_int(request->body()->length()).str() : "0"}}),
                  request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  void makeListenersAndExpectCall(const std::vector<std::string>& listener_names) {
    std::vector<std::reference_wrapper<Network::ListenerConfig>> refs;
    listeners_.clear();
    for (const auto& name : listener_names) {
      listeners_.emplace_back();
      listeners_.back().name_ = name;
      refs.push_back(listeners_.back());
    }
    EXPECT_CALL(listener_manager_, listeners()).WillOnce(Return(refs));
  }

  void addListener(Protobuf::RepeatedPtrField<envoy::api::v2::Listener>& listeners,
                   const std::string& listener_name) {
    auto listener = listeners.Add();
    listener->set_name(listener_name);
    auto socket_address = listener->mutable_address()->mutable_socket_address();
    socket_address->set_address(listener_name);
    socket_address->set_port_value(1);
    listener->add_filter_chains();
  }

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Init::MockManager init_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  MockListenerManager listener_manager_;
  Http::MockAsyncClientRequest request_;
  std::unique_ptr<LdsApiImpl> lds_;
  Event::MockTimer* interval_timer_{};
  Http::AsyncClient::Callbacks* callbacks_{};
  Api::ApiPtr api_;

private:
  std::list<NiceMock<Network::MockListenerConfig>> listeners_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(LdsApiTest, ValidateFail) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> listeners;
  listeners.Add();

  EXPECT_THROW(lds_->onConfigUpdate(listeners, ""), ProtoValidationException);
  EXPECT_CALL(request_, cancel());
}

TEST_F(LdsApiTest, UnknownCluster) {
  const std::string config_json = R"EOF(
  {
    "api_type": "REST",
    "cluster": "foo_cluster",
    "refresh_delay_ms": 1000
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  envoy::api::v2::core::ConfigSource lds_config;
  Config::Utility::translateLdsConfig(*config, lds_config);
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  EXPECT_CALL(cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      LdsApiImpl(lds_config, cluster_manager_, dispatcher_, random_, init_, local_info_, store_,
                 listener_manager_, *api_),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS "
      "cluster: 'foo_cluster' does not exist, was added via api, or is an "
      "EDS cluster");
}

TEST_F(LdsApiTest, MisconfiguredListenerNameIsPresentInException) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> listeners;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  // Construct a minimal listener that would pass proto validation.
  auto listener = listeners.Add();
  listener->set_name("invalid-listener");
  auto socket_address = listener->mutable_address()->mutable_socket_address();
  socket_address->set_address("invalid-address");
  socket_address->set_port_value(1);
  listener->add_filter_chains();

  EXPECT_CALL(listener_manager_, listeners()).WillOnce(Return(existing_listeners));

  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Throw(EnvoyException("something is wrong")));
  EXPECT_CALL(init_.initialized_, ready());

  EXPECT_THROW_WITH_MESSAGE(
      lds_->onConfigUpdate(listeners, ""), EnvoyException,
      "Error adding/updating listener(s) invalid-listener: something is wrong");
  EXPECT_CALL(request_, cancel());
}

TEST_F(LdsApiTest, EmptyListenersUpdate) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> listeners;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  EXPECT_CALL(listener_manager_, listeners()).WillOnce(Return(existing_listeners));

  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(request_, cancel());

  lds_->onConfigUpdate(listeners, "");
}

TEST_F(LdsApiTest, ListenerCreationContinuesEvenAfterException) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> listeners;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> existing_listeners;

  // Add 4 listeners - 2 valid and 2 invalid.
  addListener(listeners, "valid-listener-1");
  addListener(listeners, "invalid-listener-1");
  addListener(listeners, "valid-listener-2");
  addListener(listeners, "invalid-listener-2");

  EXPECT_CALL(listener_manager_, listeners()).WillOnce(Return(existing_listeners));

  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Return(true))
      .WillOnce(Throw(EnvoyException("something is wrong")))
      .WillOnce(Return(true))
      .WillOnce(Throw(EnvoyException("something else is wrong")));

  EXPECT_CALL(init_.initialized_, ready());

  EXPECT_THROW_WITH_MESSAGE(lds_->onConfigUpdate(listeners, ""), EnvoyException,
                            "Error adding/updating listener(s) invalid-listener-1: something is "
                            "wrong, invalid-listener-2: something else is wrong");
  EXPECT_CALL(request_, cancel());
}

// Validate onConfigUpdate throws EnvoyException with duplicate listeners.
TEST_F(LdsApiTest, ValidateDuplicateListeners) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Listener> listeners;
  auto* listener_1 = listeners.Add();
  listener_1->set_name("duplicate_listener");

  auto* listener_2 = listeners.Add();
  listener_2->set_name("duplicate_listener");

  EXPECT_THROW_WITH_MESSAGE(lds_->onConfigUpdate(listeners, ""), EnvoyException,
                            "duplicate listener duplicate_listener found");
  EXPECT_CALL(request_, cancel());
}

TEST_F(LdsApiTest, BadLocalInfo) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  const std::string config_json = R"EOF(
  {
    "api_type": "REST",
    "cluster": "foo_cluster",
    "refresh_delay_ms": 1000
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  envoy::api::v2::core::ConfigSource lds_config;
  Config::Utility::translateLdsConfig(*config, lds_config);
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockClusterMockPrioritySet cluster;
  cluster_map.emplace("foo_cluster", cluster);
  EXPECT_CALL(cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type());
  ON_CALL(local_info_, clusterName()).WillByDefault(Return(std::string()));
  EXPECT_THROW_WITH_MESSAGE(
      LdsApiImpl(lds_config, cluster_manager_, dispatcher_, random_, init_, local_info_, store_,
                 listener_manager_, *api_),
      EnvoyException,
      "lds: node 'id' and 'cluster' are required. Set it either in 'node' config or via "
      "--service-node and --service-cluster options.");
}

TEST_F(LdsApiTest, Basic) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener2",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response1_json);

  makeListenersAndExpectCall({});
  expectAdd("listener1", "0", true);
  expectAdd("listener2", "0", true);
  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ("0", lds_->versionInfo());
  EXPECT_EQ(7148434200721666028U, store_.gauge("listener_manager.lds.version").value());
  expectRequest();
  interval_timer_->callback_();

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener3",
      "address": { "socket_address": { "address": "tcp://0.0.0.3", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
  )EOF";

  message = std::make_unique<Http::ResponseMessageImpl>(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
  message->body() = std::make_unique<Buffer::OwnedImpl>(response2_json);

  makeListenersAndExpectCall({"listener1", "listener2"});
  EXPECT_CALL(listener_manager_, removeListener("listener2")).WillOnce(Return(true));
  expectAdd("listener1", "1", false);
  expectAdd("listener3", "1", true);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ("1", lds_->versionInfo());

  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_success").value());
  EXPECT_EQ(13237225503670494420U, store_.gauge("listener_manager.lds.version").value());
}

// Regression test issue #2188 where an empty ca_cert_file field was created and caused the LDS
// update to fail validation.
TEST_F(LdsApiTest, TlsConfigWithoutCaCert) {
  InSequence s;

  setup();

  std::string response1_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener0",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 61000 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response1_json);

  makeListenersAndExpectCall({"listener0"});
  expectAdd("listener0", {}, true);
  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  std::string response2_basic = R"EOF(
{{
  "version_info": "1",
  "resources": [
    {{
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener-8080",
      "address": {{ "socket_address": {{ "address": "tcp://0.0.0.0", "port_value": 61001 }} }},
      "filter_chains": [ {{
        "tls_context": {{
           "common_tls_context": {{
             "tls_certificates": [ {{
               "certificate_chain": {{ "filename": "{}" }},
               "private_key": {{ "filename": "{}" }}
              }} ]
            }}
        }},
        "filters": null }} ]
    }}
  ]
}}
  )EOF";
  std::string response2_json =
      fmt::format(response2_basic,
                  TestEnvironment::runfilesPath("/test/config/integration/certs/servercert.pem"),
                  TestEnvironment::runfilesPath("/test/config/integration/certs/serverkey.pem"));

  message = std::make_unique<Http::ResponseMessageImpl>(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
  message->body() = std::make_unique<Buffer::OwnedImpl>(response2_json);
  makeListenersAndExpectCall({
      "listener-8080",
  });
  // Can't check version here because of bazel sandbox paths for the certs.
  expectAdd("listener-8080", {}, true);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_NO_THROW(callbacks_->onSuccess(std::move(message)));
}

TEST_F(LdsApiTest, Failure) {
  InSequence s;

  setup();

  // To test the case of valid JSON with invalid config, create 2 listeners with
  // the same name.
  const std::string response_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.3", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response_json);

  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ("", lds_->versionInfo());

  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_attempt").value());
  EXPECT_EQ(1UL, store_.counter("listener_manager.lds.update_failure").value());
  // Validate that the schema error increments update_rejected stat.
  EXPECT_EQ(1UL, store_.counter("listener_manager.lds.update_failure").value());
  EXPECT_EQ(0UL, store_.gauge("listener_manager.lds.version").value());
}

TEST_F(LdsApiTest, ReplacingListenerWithSameAddress) {
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener2",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response1_json);

  makeListenersAndExpectCall({});
  expectAdd("listener1", "0", true);
  expectAdd("listener2", "0", true);
  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ("0", lds_->versionInfo());
  EXPECT_EQ(7148434200721666028U, store_.gauge("listener_manager.lds.version").value());
  expectRequest();
  interval_timer_->callback_();

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener1",
      "address": { "socket_address": { "address": "tcp://0.0.0.1", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Listener",
      "name": "listener3",
      "address": { "socket_address": { "address": "tcp://0.0.0.2", "port_value": 0 } },
      "filter_chains": [ { "filters": null } ]
    }
  ]
}
)EOF";

  message = std::make_unique<Http::ResponseMessageImpl>(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
  message->body() = std::make_unique<Buffer::OwnedImpl>(response2_json);

  makeListenersAndExpectCall({"listener1", "listener2"});
  EXPECT_CALL(listener_manager_, removeListener("listener2")).WillOnce(Return(true));
  expectAdd("listener1", "1", false);
  expectAdd("listener3", "1", true);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ("1", lds_->versionInfo());
  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_success").value());
  EXPECT_EQ(13237225503670494420U, store_.gauge("listener_manager.lds.version").value());
}

} // namespace
} // namespace Server
} // namespace Envoy
