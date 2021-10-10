#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/router/rds/route_config_update_receiver_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() {
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

class Config {
public:
  Config() = default;
  Config(const envoy::config::route::v3::RouteConfiguration& rc) : rc_(rc) {}
  const std::string* route(const std::string& name) const {
    for (const auto& virtual_host_config : rc_.virtual_hosts()) {
      if (virtual_host_config.name() == name) {
        return &virtual_host_config.name();
      }
    }
    return nullptr;
  }

private:
  envoy::config::route::v3::RouteConfiguration rc_;
};

class ConfigFactory
    : public Rds::ConfigFactory<envoy::config::route::v3::RouteConfiguration, Config> {
public:
  std::shared_ptr<const Config>
  createConfig(const envoy::config::route::v3::RouteConfiguration& rc) const override {
    return std::make_shared<const Config>(rc);
  }
  std::shared_ptr<const Config> createConfig() const override {
    return std::make_shared<const Config>();
  }
};

using RouteConfigUpdatePtr = std::unique_ptr<
    Rds::RouteConfigUpdateReceiver<envoy::config::route::v3::RouteConfiguration, Config>>;
using RouteConfigUpdateReceiverImpl =
    Rds::RouteConfigUpdateReceiverImpl<envoy::config::route::v3::RouteConfiguration, Config>;

class RdsImplTest : public RdsTestBase {
public:
  ~RdsImplTest() override { server_factory_context_.thread_local_.shutdownThread(); }

  void setup() {
    config_update_ =
        std::make_unique<RouteConfigUpdateReceiverImpl>(server_factory_context_, config_factory_);
  }

  const std::string* route(const std::string& path) {
    return config_update_->parsedConfiguration()->route(path);
  }

  ConfigFactory config_factory_;
  RouteConfigUpdatePtr config_update_;
};

TEST_F(RdsImplTest, Basic) {
  setup();

  EXPECT_TRUE(config_update_->parsedConfiguration());
  EXPECT_EQ(nullptr, config_update_->parsedConfiguration()->route("foo"));
  EXPECT_FALSE(config_update_->configInfo().has_value());

  const std::string response1_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": null
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response1_json);

  SystemTime time1(std::chrono::milliseconds(1234567891234));
  timeSystem().setSystemTime(time1);

  EXPECT_TRUE(config_update_->onRdsUpdate(response1, "1"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_EQ("1", config_update_->configVersion());
  EXPECT_EQ(time1, config_update_->lastUpdated());
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ(config_update_->configVersion(), config_update_->configInfo().value().version_);

  EXPECT_FALSE(config_update_->onRdsUpdate(response1, "2"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_EQ("1", config_update_->configVersion());

  std::shared_ptr<const Config> config = config_update_->parsedConfiguration();
  EXPECT_EQ(2, config.use_count());

  const std::string response2_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": [
    {
      "name": "foo",
      "domains": [
        "*"
      ],
    }
  ]
}
  )EOF";

  auto response2 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response2_json);

  SystemTime time2(std::chrono::milliseconds(1234567891235));
  timeSystem().setSystemTime(time2);

  EXPECT_TRUE(config_update_->onRdsUpdate(response2, "2"));
  EXPECT_EQ("foo", *route("foo"));
  EXPECT_EQ("2", config_update_->configVersion());
  EXPECT_EQ(time2, config_update_->lastUpdated());
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ(config_update_->configVersion(), config_update_->configInfo().value().version_);

  EXPECT_EQ(1, config.use_count());
}

} // namespace
} // namespace Router
} // namespace Envoy
