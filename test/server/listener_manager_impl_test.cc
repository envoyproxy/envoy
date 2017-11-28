#include "envoy/registry/registry.h"

#include "common/network/address_impl.h"

#include "server/configuration_impl.h"
#include "server/listener_manager_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::Throw;
using testing::_;

namespace Envoy {
namespace Server {

class ListenerHandle {
public:
  ListenerHandle() { EXPECT_CALL(*drain_manager_, startParentShutdownSequence()).Times(0); }
  ~ListenerHandle() { onDestroy(); }

  MOCK_METHOD0(onDestroy, void());

  Init::MockTarget target_;
  MockDrainManager* drain_manager_ = new MockDrainManager();
  Configuration::FactoryContext* context_{};
};

class ListenerManagerImplTest : public testing::Test {
public:
  ListenerManagerImplTest() {
    EXPECT_CALL(worker_factory_, createWorker_()).WillOnce(Return(worker_));
    manager_.reset(new ListenerManagerImpl(server_, listener_factory_, worker_factory_));
  }

  /**
   * This routing sets up an expectation that does various things:
   * 1) Allows us to track listener destruction via filter factory destruction.
   * 2) Allows us to register for init manager handling much like RDS, etc. would do.
   * 3) Stores the factory context for later use.
   * 4) Creates a mock local drain manager for the listener.
   */
  ListenerHandle* expectListenerCreate(
      bool need_init,
      envoy::api::v2::Listener::DrainType drain_type = envoy::api::v2::Listener_DrainType_DEFAULT) {
    ListenerHandle* raw_listener = new ListenerHandle();
    EXPECT_CALL(listener_factory_, createDrainManager_(drain_type))
        .WillOnce(Return(raw_listener->drain_manager_));
    EXPECT_CALL(listener_factory_, createFilterFactoryList(_, _))
        .WillOnce(Invoke(
            [raw_listener, need_init](const Protobuf::RepeatedPtrField<envoy::api::v2::Filter>&,
                                      Configuration::FactoryContext& context)
                -> std::vector<Configuration::NetworkFilterFactoryCb> {
              std::shared_ptr<ListenerHandle> notifier(raw_listener);
              raw_listener->context_ = &context;
              if (need_init) {
                context.initManager().registerTarget(notifier->target_);
              }
              return {[notifier](Network::FilterManager&) -> void {}};
            }));

    return raw_listener;
  }

  void checkStats(uint64_t added, uint64_t modified, uint64_t removed, uint64_t warming,
                  uint64_t active, uint64_t draining) {
    EXPECT_EQ(added, server_.stats_store_.counter("listener_manager.listener_added").value());
    EXPECT_EQ(modified, server_.stats_store_.counter("listener_manager.listener_modified").value());
    EXPECT_EQ(removed, server_.stats_store_.counter("listener_manager.listener_removed").value());
    EXPECT_EQ(warming,
              server_.stats_store_.gauge("listener_manager.total_listeners_warming").value());
    EXPECT_EQ(active,
              server_.stats_store_.gauge("listener_manager.total_listeners_active").value());
    EXPECT_EQ(draining,
              server_.stats_store_.gauge("listener_manager.total_listeners_draining").value());
  }

  NiceMock<MockInstance> server_;
  NiceMock<MockListenerComponentFactory> listener_factory_;
  MockWorker* worker_ = new MockWorker();
  NiceMock<MockWorkerFactory> worker_factory_;
  std::unique_ptr<ListenerManagerImpl> manager_;
  NiceMock<MockGuardDog> guard_dog_;
};

class ListenerManagerImplWithRealFiltersTest : public ListenerManagerImplTest {
public:
  ListenerManagerImplWithRealFiltersTest() {
    // Use real filter loading by default.
    ON_CALL(listener_factory_, createFilterFactoryList(_, _))
        .WillByDefault(Invoke([](const Protobuf::RepeatedPtrField<envoy::api::v2::Filter>& filters,
                                 Configuration::FactoryContext& context)
                                  -> std::vector<Configuration::NetworkFilterFactoryCb> {
          return ProdListenerComponentFactory::createFilterFactoryList_(filters, context);
        }));
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, EmptyFilter) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromJson(json));
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, DefaultListenerPerConnectionBufferLimit) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromJson(json));
  EXPECT_EQ(1024 * 1024U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SetListenerPerConnectionBufferLimit) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "per_connection_buffer_limit_bytes": 8192
  }
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromJson(json));
  EXPECT_EQ(8192U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SslContext) {
  const std::string json = TestEnvironment::substitute(R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters" : [],
    "ssl_context" : {
      "cert_chain_file" : "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem",
      "private_key_file" : "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem",
      "verify_subject_alt_name" : [
        "localhost",
        "127.0.0.1"
      ]
    }
  }
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromJson(json));
  EXPECT_NE(nullptr, manager_->listeners().back().get().defaultSslContext());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfig) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "test": "a"
  }
  )EOF";

  EXPECT_THROW(manager_->addOrUpdateListener(parseListenerFromJson(json)), Json::Exception);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterConfig) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      {
        "type" : "type",
        "name" : "name",
        "config" : {}
      }
    ]
  }
  )EOF";

  EXPECT_THROW(manager_->addOrUpdateListener(parseListenerFromJson(json)), Json::Exception);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterName) {
  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      {
        "type" : "write",
        "name" : "invalid",
        "config" : {}
      }
    ]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromJson(json)),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'invalid'");
}

class TestStatsConfigFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Configuration::NamedNetworkFilterConfigFactory
  Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object&, Configuration::FactoryContext& context) override {
    context.scope().counter("bar").inc();
    return [](Network::FilterManager&) -> void {};
  }
  std::string name() override { return "stats_test"; }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, StatsScopeTest) {
  Registry::RegisterFactory<TestStatsConfigFactory, Configuration::NamedNetworkFilterConfigFactory>
      registered;

  const std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "bind_to_port": false,
    "filters": [
      {
        "type" : "read",
        "name" : "stats_test",
        "config" : {}
      }
    ]
  }
  )EOF";

  EXPECT_CALL(listener_factory_, createListenSocket(_, false));
  manager_->addOrUpdateListener(parseListenerFromJson(json));
  manager_->listeners().front().get().listenerScope().counter("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counter("bar").value());
  EXPECT_EQ(1UL, server_.stats_store_.counter("listener.127.0.0.1_1234.foo").value());
}

TEST_F(ListenerManagerImplTest, ModifyOnlyDrainType) {
  InSequence s;

  // Add foo listener.
  const std::string listener_foo_yaml = R"EOF(
    name: "foo"
    address:
      socket_address: { address: 127.0.0.1, port_value: 10000 }
    filter_chains:
    - filters:
    drain_type: MODIFY_ONLY
  )EOF";

  ListenerHandle* listener_foo =
      expectListenerCreate(false, envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(listener_foo_yaml)));
  checkStats(1, 0, 0, 0, 1, 0);

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddListenerAddressNotMatching) {
  InSequence s;

  // Add foo listener.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "drain_type": "default"
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update foo listener, but with a different address. Should throw.
  const std::string listener_foo_different_address_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1235",
    "filters": [],
    "drain_type": "modify_only"
  }
  )EOF";

  ListenerHandle* listener_foo_different_address =
      expectListenerCreate(false, envoy::api::v2::Listener_DrainType_MODIFY_ONLY);
  EXPECT_CALL(*listener_foo_different_address, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_different_address_json)),
      EnvoyException,
      "error updating listener: 'foo' has a different address "
      "'127.0.0.1:1235' from existing listener");

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddOrUpdateListener) {
  InSequence s;

  // Add foo listener.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update foo listener. Should share socket.
  const std::string listener_foo_update1_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(false);
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_update1_json)));
  checkStats(1, 1, 0, 0, 1, 0);

  // Start workers.
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);
  worker_->callAddCompletion(true);

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_update1_json)));
  checkStats(1, 1, 0, 0, 1, 0);

  // Update foo. Should go into warming, have an immediate warming callback, and start immediate
  // removal.
  ListenerHandle* listener_foo_update2 = expectListenerCreate(false);
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo_update1->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  worker_->callAddCompletion(true);
  checkStats(1, 2, 0, 0, 1, 1);

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update1->drain_manager_->drain_sequence_completion_();
  checkStats(1, 2, 0, 0, 1, 1);
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(1, 2, 0, 0, 1, 0);

  // Add bar listener.
  const std::string listener_bar_json = R"EOF(
  {
    "name": "bar",
    "address": "tcp://127.0.0.1:1235",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_bar = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_bar_json)));
  EXPECT_EQ(2UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(2, 2, 0, 0, 2, 0);

  // Add baz listener, this time requiring initializing.
  const std::string listener_baz_json = R"EOF(
  {
    "name": "baz",
    "address": "tcp://127.0.0.1:1236",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_baz = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(listener_baz->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_baz_json)));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 2, 0, 1, 2, 0);

  // Update a duplicate baz that is currently warming.
  EXPECT_FALSE(manager_->addOrUpdateListener(parseListenerFromJson(listener_baz_json)));
  checkStats(3, 2, 0, 1, 2, 0);

  // Update baz while it is warming.
  const std::string listener_baz_update1_json = R"EOF(
  {
    "name": "baz",
    "address": "tcp://127.0.0.1:1236",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  ListenerHandle* listener_baz_update1 = expectListenerCreate(true);
  EXPECT_CALL(*listener_baz, onDestroy()).WillOnce(Invoke([listener_baz]() -> void {
    // Call the initialize callback during destruction like RDS will.
    listener_baz->target_.callback_();
  }));
  EXPECT_CALL(listener_baz_update1->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_baz_update1_json)));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 3, 0, 1, 2, 0);

  // Finish initialization for baz which should make it active.
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_baz_update1->target_.callback_();
  EXPECT_EQ(3UL, manager_->listeners().size());
  worker_->callAddCompletion(true);
  checkStats(3, 3, 0, 0, 3, 0);

  EXPECT_CALL(*listener_foo_update2, onDestroy());
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_CALL(*listener_baz_update1, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddDrainingListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener directly into active.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Network::Address::InstanceConstSharedPtr local_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 1234));
  ON_CALL(*listener_factory_.socket_, localAddress()).WillByDefault(Return(local_address));

  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  worker_->callAddCompletion(true);
  checkStats(1, 0, 0, 0, 1, 0);

  // Remove foo into draining.
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(1, 0, 1, 0, 0, 1);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(1, 0, 1, 0, 0, 1);

  // Add foo again. We should use the socket from draining.
  ListenerHandle* listener_foo2 = expectListenerCreate(false);
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  worker_->callAddCompletion(true);
  checkStats(2, 0, 1, 0, 1, 1);

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(2, 0, 1, 0, 1, 0);

  EXPECT_CALL(*listener_foo2, onDestroy());
}

TEST_F(ListenerManagerImplTest, CantBindSocket) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true))
      .WillOnce(Throw(EnvoyException("can't bind")));
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_THROW(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)),
               EnvoyException);
}

TEST_F(ListenerManagerImplTest, ListenerDraining) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  worker_->callAddCompletion(true);
  checkStats(1, 0, 0, 0, 1, 0);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_FALSE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(1, 0, 1, 0, 0, 1);

  // NOTE: || short circuit here prevents the server drain manager from getting called.
  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(1, 0, 1, 0, 0, 1);

  EXPECT_CALL(*listener_foo->drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(server_.drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_TRUE(listener_foo->context_->drainDecision().drainClose());

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 1, 0, 0, 0);
}

TEST_F(ListenerManagerImplTest, RemoveListener) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Remove an unknown listener.
  EXPECT_FALSE(manager_->removeListener("unknown"));

  // Add foo listener into warming.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(listener_foo->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 0, 1, 0, 0);

  // Remove foo.
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->removeListener("foo"));
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(1, 0, 1, 0, 0, 0);

  // Add foo again and initialize it.
  listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(listener_foo->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));
  checkStats(2, 0, 1, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.callback_();
  worker_->callAddCompletion(true);
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(2, 0, 1, 0, 1, 0);

  // Update foo into warming.
  const std::string listener_foo_update1_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  ListenerHandle* listener_foo_update1 = expectListenerCreate(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_update1_json)));
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(2, 1, 1, 1, 1, 0);

  // Remove foo which should remove both warming and active.
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->removeListener("foo"));
  checkStats(2, 1, 2, 0, 0, 1);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();
  checkStats(2, 1, 2, 0, 0, 1);
  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();
  EXPECT_EQ(0UL, manager_->listeners().size());
  checkStats(2, 1, 2, 0, 0, 0);
}

TEST_F(ListenerManagerImplTest, AddListenerFailure) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into active.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://0.0.0.0:1234",
    "filters": []
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_, _));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));

  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo->drain_manager_, startDrainSequence(_));
  worker_->callAddCompletion(false);

  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo->drain_manager_->drain_sequence_completion_();

  EXPECT_CALL(*listener_foo, onDestroy());
  worker_->callRemovalCompletion();

  EXPECT_EQ(1UL, server_.stats_store_.counter("listener_manager.listener_create_failure").value());
}

TEST_F(ListenerManagerImplTest, StatsNameValidCharacterTest) {
  const std::string json = R"EOF(
  {
    "address": "tcp://[::1]:10000",
    "filters": [],
    "bind_to_port": false
  }
  )EOF";

  manager_->addOrUpdateListener(parseListenerFromJson(json));
  manager_->listeners().front().get().listenerScope().counter("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counter("listener.[__1]_10000.foo").value());
}

TEST_F(ListenerManagerImplTest, DuplicateAddressDontBind) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Add foo listener into warming.
  const std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://0.0.0.0:1234",
    "filters": [],
    "bind_to_port": false
  }
  )EOF";

  ListenerHandle* listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, false));
  EXPECT_CALL(listener_foo->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(parseListenerFromJson(listener_foo_json)));

  // Add bar with same non-binding address. Should fail.
  const std::string listener_bar_json = R"EOF(
  {
    "name": "bar",
    "address": "tcp://0.0.0.0:1234",
    "filters": [],
    "bind_to_port": false
  }
  )EOF";

  ListenerHandle* listener_bar = expectListenerCreate(true);
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromJson(listener_bar_json)), EnvoyException,
      "error adding listener: 'bar' has duplicate address '0.0.0.0:1234' as existing listener");

  // Move foo to active and then try to add again. This should still fail.
  EXPECT_CALL(*worker_, addListener(_, _));
  listener_foo->target_.callback_();
  worker_->callAddCompletion(true);

  listener_bar = expectListenerCreate(true);
  EXPECT_CALL(*listener_bar, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(
      manager_->addOrUpdateListener(parseListenerFromJson(listener_bar_json)), EnvoyException,
      "error adding listener: 'bar' has duplicate address '0.0.0.0:1234' as existing listener");

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, EarlyShutdown) {
  // If stopWorkers is called before the workers are started, it should be a no-op: they should be
  // neither started nor stopped.
  EXPECT_CALL(*worker_, start(_)).Times(0);
  EXPECT_CALL(*worker_, stop()).Times(0);
  manager_->stopWorkers();
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SniWithSingleFilterChain) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        sni_domains: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml));
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SniWithTwoEqualFilterChains) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        sni_domains: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
    - filter_chain_match:
        sni_domains: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml));
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       SniWithTwoEqualFilterChainsWithDifferentSessionTicketKeys) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        sni_domains: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
    - filter_chain_match:
        sni_domains: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b"
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml));
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest,
       SniWithTwoEqualFilterChainsWithMixedUseOfSessionTicketKeys) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        sni_domains: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
        session_ticket_keys:
          keys:
          - filename: "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a"
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
    - filter_chain_match:
        sni_domains: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml)),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': filter chains with mixed use "
                            "of Session Ticket Keys are currently not supported");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SniWithTwoDifferentFilterChains) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    address:
      socket_address: { address: 127.0.0.1, port_value: 1234 }
    filter_chains:
    - filter_chain_match:
        sni_domains: "example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_foo } 
    - filter_chain_match:
        sni_domains: "www.example.com"
      tls_context:
        common_tls_context:
          tls_certificates:
            - certificate_chain: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem" }
              private_key: { filename: "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem" }
      filters:
      - name: envoy.http_connection_manager
        config:
          route_config:
            virtual_hosts:
            - routes:
              - match: { prefix: "/" }
                route: { cluster: service_bar } 
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(parseListenerFromV2Yaml(yaml)),
                            EnvoyException,
                            "error adding listener '127.0.0.1:1234': use of different filter "
                            "chains is currently not supported");
}

} // namespace Server
} // namespace Envoy
