#include "envoy/registry/registry.h"

#include "common/network/address_impl.h"

#include "server/configuration_impl.h"
#include "server/listener_manager_impl.h"

#include "test/mocks/server/mocks.h"
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
  ListenerHandle* expectListenerCreate(bool need_init) {
    ListenerHandle* raw_listener = new ListenerHandle();
    EXPECT_CALL(listener_factory_, createDrainManager_())
        .WillOnce(Return(raw_listener->drain_manager_));
    EXPECT_CALL(listener_factory_, createFilterFactoryList(_, _))
        .WillOnce(Invoke([raw_listener, need_init](const std::vector<Json::ObjectSharedPtr>&,
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
        .WillByDefault(Invoke([this](const std::vector<Json::ObjectSharedPtr>& filters,
                                     Configuration::FactoryContext& context)
                                  -> std::vector<Configuration::NetworkFilterFactoryCb> {
          return ProdListenerComponentFactory::createFilterFactoryList_(filters, server_, context);
        }));
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, EmptyFilter) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(server_.random_, uuid());
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(*loader);
  EXPECT_EQ(1U, manager_->listeners().size());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, DefaultListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(*loader);
  EXPECT_EQ(1024 * 1024U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SetListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "per_connection_buffer_limit_bytes": 8192
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(*loader);
  EXPECT_EQ(8192U, manager_->listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, SslContext) {
  std::string json = R"EOF(
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
  )EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(*loader);
  EXPECT_NE(nullptr, manager_->listeners().back().get().sslContext());
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadListenerConfig) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "test": "a"
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW(manager_->addOrUpdateListener(*loader), Json::Exception);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterConfig) {
  std::string json = R"EOF(
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

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW(manager_->addOrUpdateListener(*loader), Json::Exception);
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterName) {
  std::string json = R"EOF(
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

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(*loader), EnvoyException,
                            "unable to create filter factory for 'invalid'/'write'");
}

TEST_F(ListenerManagerImplWithRealFiltersTest, BadFilterType) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      {
        "type" : "write",
        "name" : "echo",
        "config" : {}
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(*loader), EnvoyException,
                            "unable to create filter factory for 'echo'/'write'");
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
  Configuration::NetworkFilterType type() override {
    return Configuration::NetworkFilterType::Read;
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, StatsScopeTest) {
  Registry::RegisterFactory<TestStatsConfigFactory, Configuration::NamedNetworkFilterConfigFactory>
      registered;

  std::string json = R"EOF(
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

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(listener_factory_, createListenSocket(_, false));
  manager_->addOrUpdateListener(*loader);
  manager_->listeners().front().get().listenerScope().counter("foo").inc();

  EXPECT_EQ(1UL, server_.stats_store_.counter("bar").value());
  EXPECT_EQ(1UL, server_.stats_store_.counter("listener.127.0.0.1_1234.foo").value());
}

/**
 * Config registration for the echo filter using the deprecated registration class.
 */
class TestDeprecatedEchoConfigFactory : public Configuration::NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  Configuration::NetworkFilterFactoryCb
  tryCreateFilterFactory(Configuration::NetworkFilterType type, const std::string& name,
                         const Json::Object&, Instance&) override {
    if (type != Configuration::NetworkFilterType::Read || name != "echo_deprecated") {
      return nullptr;
    }

    return [](Network::FilterManager&) -> void {};
  }
};

TEST_F(ListenerManagerImplWithRealFiltersTest, DeprecatedFilterConfigFactoryRegistrationTest) {
  // Test ensures that the deprecated network filter registration still works without error.

  // Register the config factory
  Configuration::RegisterNetworkFilterConfigFactory<TestDeprecatedEchoConfigFactory> registered;

  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      {
        "type" : "read",
        "name" : "echo_deprecated",
        "config" : {}
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  manager_->addOrUpdateListener(*loader);
}

TEST_F(ListenerManagerImplTest, AddListenerAddressNotMatching) {
  InSequence s;

  // Add foo listener.
  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update foo listener, but with a different address. Should throw.
  std::string listener_foo_different_address_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1235",
    "filters": []
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_foo_different_address_json);
  ListenerHandle* listener_foo_different_address = expectListenerCreate(false);
  EXPECT_CALL(*listener_foo_different_address, onDestroy());
  EXPECT_THROW_WITH_MESSAGE(manager_->addOrUpdateListener(*loader), EnvoyException,
                            "error updating listener: 'foo' has a different address "
                            "'127.0.0.1:1235' from existing listener");

  EXPECT_CALL(*listener_foo, onDestroy());
}

TEST_F(ListenerManagerImplTest, AddOrUpdateListener) {
  InSequence s;

  // Add foo listener.
  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 0, 0, 0, 1, 0);

  // Update foo listener. Should share socket.
  std::string listener_foo_update1_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_foo_update1_json);
  ListenerHandle* listener_foo_update1 = expectListenerCreate(false);
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 1, 0, 0, 1, 0);

  // Start workers.
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  // Update duplicate should be a NOP.
  EXPECT_FALSE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 1, 0, 0, 1, 0);

  // Update foo. Should go into warming, have an immediate warming callback, and start immediate
  // removal.
  loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo_update2 = expectListenerCreate(false);
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_CALL(*worker_, stopListener(_));
  EXPECT_CALL(*listener_foo_update1->drain_manager_, startDrainSequence(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  checkStats(1, 2, 0, 0, 1, 1);
  EXPECT_CALL(*worker_, removeListener(_, _));
  listener_foo_update1->drain_manager_->drain_sequence_completion_();
  checkStats(1, 2, 0, 0, 1, 1);
  EXPECT_CALL(*listener_foo_update1, onDestroy());
  worker_->callRemovalCompletion();
  checkStats(1, 2, 0, 0, 1, 0);

  // Add bar listener.
  std::string listener_bar_json = R"EOF(
  {
    "name": "bar",
    "address": "tcp://127.0.0.1:1235",
    "filters": []
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_bar_json);
  ListenerHandle* listener_bar = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(2, 2, 0, 0, 2, 0);

  // Add baz listener, this time requiring initializing.
  std::string listener_baz_json = R"EOF(
  {
    "name": "baz",
    "address": "tcp://127.0.0.1:1236",
    "filters": []
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_baz_json);
  ListenerHandle* listener_baz = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(listener_baz->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 2, 0, 1, 2, 0);

  // Update a duplicate baz that is currently warming.
  EXPECT_FALSE(manager_->addOrUpdateListener(*loader));
  checkStats(3, 2, 0, 1, 2, 0);

  // Update baz while it is warming.
  std::string listener_baz_update1_json = R"EOF(
  {
    "name": "baz",
    "address": "tcp://127.0.0.1:1236",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_baz_update1_json);
  ListenerHandle* listener_baz_update1 = expectListenerCreate(true);
  EXPECT_CALL(*listener_baz, onDestroy()).WillOnce(Invoke([listener_baz]() -> void {
    // Call the initialize callback during destruction like RDS will.
    listener_baz->target_.callback_();
  }));
  EXPECT_CALL(listener_baz_update1->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  EXPECT_EQ(2UL, manager_->listeners().size());
  checkStats(3, 3, 0, 1, 2, 0);

  // Finish initialization for baz which should make it active.
  EXPECT_CALL(*worker_, addListener(_));
  listener_baz_update1->target_.callback_();
  EXPECT_EQ(3UL, manager_->listeners().size());
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
  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Network::Address::InstanceConstSharedPtr local_address(
      new Network::Address::Ipv4Instance("127.0.0.1", 1234));
  ON_CALL(*listener_factory_.socket_, localAddress()).WillByDefault(Return(local_address));

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
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
  loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo2 = expectListenerCreate(false);
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
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

  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true))
      .WillOnce(Throw(EnvoyException("can't bind")));
  EXPECT_CALL(*listener_foo, onDestroy());
  EXPECT_THROW(manager_->addOrUpdateListener(*loader), EnvoyException);
}

TEST_F(ListenerManagerImplTest, ListenerDraining) {
  InSequence s;

  EXPECT_CALL(*worker_, start(_));
  manager_->startWorkers(guard_dog_);

  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(false);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(*worker_, addListener(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
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
  std::string listener_foo_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(listener_foo_json);
  ListenerHandle* listener_foo = expectListenerCreate(true);
  EXPECT_CALL(listener_factory_, createListenSocket(_, true));
  EXPECT_CALL(listener_foo->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
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
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
  checkStats(2, 0, 1, 1, 0, 0);
  EXPECT_CALL(*worker_, addListener(_));
  listener_foo->target_.callback_();
  EXPECT_EQ(1UL, manager_->listeners().size());
  checkStats(2, 0, 1, 0, 1, 0);

  // Update foo into warming.
  std::string listener_foo_update1_json = R"EOF(
  {
    "name": "foo",
    "address": "tcp://127.0.0.1:1234",
    "filters": [
      { "type" : "read", "name" : "fake", "config" : {} }
    ]
  }
  )EOF";

  loader = Json::Factory::loadFromString(listener_foo_update1_json);
  ListenerHandle* listener_foo_update1 = expectListenerCreate(true);
  EXPECT_CALL(listener_foo_update1->target_, initialize(_));
  EXPECT_TRUE(manager_->addOrUpdateListener(*loader));
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

} // namespace Server
} // namespace Envoy
