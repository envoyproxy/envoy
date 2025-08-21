#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.h"

#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
class HttpServerPropertiesCacheManagerTest : public testing::Test,
                                             public Event::TestUsingSimulatedTime {
public:
  HttpServerPropertiesCacheManagerTest() {
    options_.set_name("name");
    options_.mutable_max_entries()->set_value(10);
  }
  void initialize() {
    manager_ = std::make_unique<Http::HttpServerPropertiesCacheManagerImpl>(
        context_.server_factory_context_, context_.messageValidationVisitor(), tls_);
  }
  Singleton::ManagerImpl singleton_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Http::HttpServerPropertiesCacheManagerSharedPtr manager_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions options_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
};

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithFlushingAndConcurrency) {
  envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig config;
  config.set_filename("foo");
  envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
  kv_config.mutable_config()->set_name("envoy.key_value.file_based");
  kv_config.mutable_config()->mutable_typed_config()->PackFrom(config);
  options_.mutable_key_value_store_config()->set_name("envoy.common.key_value");
  options_.mutable_key_value_store_config()->mutable_typed_config()->PackFrom(kv_config);
  initialize();
  manager_->getCache(options_, dispatcher_);
}

} // namespace
} // namespace Envoy
