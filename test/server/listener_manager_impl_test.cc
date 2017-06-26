#include "envoy/registry/registry.h"

#include "server/configuration_impl.h"
#include "server/listener_manager_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Server {

class ListenerManagerImplTest : public testing::Test {
public:
  ListenerManagerImplTest() {
    // Use real filter loading by default.
    ON_CALL(factory_, createFilterFactoryList(_, _))
        .WillByDefault(
            Invoke([&](const std::vector<Json::ObjectSharedPtr>& filters,
                       Server::Configuration::FactoryContext& context)
                       -> std::vector<Server::Configuration::NetworkFilterFactoryCb> {
                         return Server::ProdListenerComponentFactory::createFilterFactoryList_(
                             filters, server_, context);
                       }));
  }

  NiceMock<MockInstance> server_;
  MockListenerComponentFactory factory_;
  ListenerManagerImpl manager_{server_, factory_};
};

TEST_F(ListenerManagerImplTest, EmptyFilter) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, true));
  manager_.addListener(*loader);
  EXPECT_EQ(1U, manager_.listeners().size());
}

TEST_F(ListenerManagerImplTest, DefaultListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": []
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, true));
  manager_.addListener(*loader);
  EXPECT_EQ(1024 * 1024U, manager_.listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplTest, SetListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "per_connection_buffer_limit_bytes": 8192
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, true));
  manager_.addListener(*loader);
  EXPECT_EQ(8192U, manager_.listeners().back().get().perConnectionBufferLimitBytes());
}

TEST_F(ListenerManagerImplTest, SslContext) {
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
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, true));
  manager_.addListener(*loader);
  EXPECT_NE(nullptr, manager_.listeners().back().get().sslContext());
}

TEST_F(ListenerManagerImplTest, BadListenerConfig) {
  std::string json = R"EOF(
  {
    "address": "tcp://127.0.0.1:1234",
    "filters": [],
    "test": "a"
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW(manager_.addListener(*loader), Json::Exception);
}

TEST_F(ListenerManagerImplTest, BadFilterConfig) {
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
  EXPECT_THROW(manager_.addListener(*loader), Json::Exception);
}

TEST_F(ListenerManagerImplTest, BadFilterName) {
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
  EXPECT_THROW_WITH_MESSAGE(manager_.addListener(*loader), EnvoyException,
                            "unable to create filter factory for 'invalid'/'write'");
}

TEST_F(ListenerManagerImplTest, BadFilterType) {
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
  EXPECT_THROW_WITH_MESSAGE(manager_.addListener(*loader), EnvoyException,
                            "unable to create filter factory for 'echo'/'write'");
}

class TestStatsConfigFactory : public Configuration::NamedNetworkFilterConfigFactory {
public:
  // Server::Configuration::NamedNetworkFilterConfigFactory
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

TEST_F(ListenerManagerImplTest, StatsScopeTest) {
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
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, false));
  manager_.addListener(*loader);
  manager_.listeners().front().get().listenerScope().counter("foo").inc();

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
                         const Json::Object&, Server::Instance&) override {
    if (type != Configuration::NetworkFilterType::Read || name != "echo_deprecated") {
      return nullptr;
    }

    return [](Network::FilterManager&) -> void {};
  }
};

TEST_F(ListenerManagerImplTest, DeprecatedFilterConfigFactoryRegistrationTest) {
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
  EXPECT_CALL(factory_, createFilterFactoryList(_, _));
  EXPECT_CALL(factory_, createListenSocket_(_, true));
  manager_.addListener(*loader);
}

} // Server
} // Envoy
