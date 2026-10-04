#include "envoy/common/key_value_store.h"
#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/core/v3/extension.pb.h"

#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

namespace {

// A key value store factory that records how it was asked to validate its configuration. Its
// configuration proto is TypedExtensionConfig, whose name field carries a PGV constraint.
class TestKeyValueStoreFactory : public KeyValueStoreFactory {
public:
  KeyValueStorePtr createStore(const Protobuf::Message&,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Event::Dispatcher&, Filesystem::Instance&) override {
    last_store_skipped_validation_ = validation_visitor.skipValidation();
    return std::make_unique<testing::NiceMock<MockKeyValueStore>>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
  }
  std::string name() const override { return "envoy.key_value.test"; }

  std::optional<bool> last_store_skipped_validation_;
};

class HttpServerPropertiesCacheManagerTest : public testing::Test,
                                             public Event::TestUsingSimulatedTime {
public:
  HttpServerPropertiesCacheManagerTest() {
    options1_.set_name(name1_);
    options1_.mutable_max_entries()->set_value(max_entries1_);

    options2_.set_name(name2_);
    options2_.mutable_max_entries()->set_value(max_entries2_);
  }
  void initialize() { initialize(context_.messageValidationVisitor()); }
  void initialize(ProtobufMessage::ValidationVisitor& validation_visitor) {
    manager_ = std::make_unique<HttpServerPropertiesCacheManagerImpl>(
        context_.server_factory_context_, validation_visitor, tls_);
  }

  // A configuration for the test store factory with the given name. TypedExtensionConfig requires
  // a non-empty name and a typed_config, so an empty name makes the configuration invalid.
  static envoy::config::core::v3::TypedExtensionConfig testStoreConfig(absl::string_view name) {
    envoy::config::core::v3::TypedExtensionConfig store_config;
    store_config.set_name(std::string(name));
    MessageUtil::packFrom(*store_config.mutable_typed_config(), Protobuf::Struct());
    return store_config;
  }

  // Configures a key value store on the options with the given store specific configuration,
  // using the test factory unless another factory name is given.
  static void
  setKeyValueStoreConfig(envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
                         const Protobuf::Message& store_config,
                         absl::string_view factory_name = "envoy.key_value.test") {
    envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
    kv_config.mutable_config()->set_name(std::string(factory_name));
    MessageUtil::packFrom(*kv_config.mutable_config()->mutable_typed_config(), store_config);
    options.mutable_key_value_store_config()->set_name("envoy.common.key_value");
    MessageUtil::packFrom(*options.mutable_key_value_store_config()->mutable_typed_config(),
                          kv_config);
  }

  TestKeyValueStoreFactory test_store_factory_;
  Registry::InjectFactory<KeyValueStoreFactory> inject_store_factory_{test_store_factory_};

  Singleton::ManagerImpl singleton_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  HttpServerPropertiesCacheManagerSharedPtr manager_;
  const std::string name1_ = "name1";
  const std::string name2_ = "name2";
  const int max_entries1_ = 10;
  const int max_entries2_ = 20;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::config::core::v3::AlternateProtocolsCacheOptions options1_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions options2_;
};

TEST_F(HttpServerPropertiesCacheManagerTest, GetCache) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithEntry) {
  auto* entry = options1_.add_prepopulated_entries();
  entry->set_hostname("foo.com");
  entry->set_port(1);

  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));

  const HttpServerPropertiesCacheImpl::Origin origin = {"https", entry->hostname(), entry->port()};
  EXPECT_TRUE(cache->findAlternatives(origin).has_value());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithInvalidCanonicalEntry) {
  auto* suffixes = options1_.add_canonical_suffixes();
  *suffixes = "example.com";

  initialize();
  EXPECT_ENVOY_BUG(manager_->getCache(options1_, dispatcher_),
                   "Suffix does not start with a leading '.': example.com");
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithCanonicalEntry) {
  auto* suffixes = options1_.add_canonical_suffixes();
  *suffixes = ".example.com";
  auto* entry = options1_.add_prepopulated_entries();
  entry->set_hostname("first.example.com");
  entry->set_port(1);

  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));

  const HttpServerPropertiesCacheImpl::Origin origin = {"https", "second.example.com",
                                                        entry->port()};
  EXPECT_TRUE(cache->findAlternatives(origin).has_value());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheForDifferentOptions) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  HttpServerPropertiesCacheSharedPtr cache2 = manager_->getCache(options2_, dispatcher_);
  EXPECT_NE(nullptr, cache2);
  EXPECT_NE(cache1, cache2);

  int num_caches = 0;
  Http::HttpServerPropertiesCacheManager::CacheFn count_caches =
      [&](Http::HttpServerPropertiesCache& cache) {
        EXPECT_TRUE(&cache == cache1.get() || &cache == cache2.get());
        ++num_caches;
      };
  manager_->forEachThreadLocalCache(count_caches);
  EXPECT_EQ(num_caches, 2);
}

TEST_F(HttpServerPropertiesCacheManagerTest, ValidateOptionsWithoutKeyValueStore) {
  initialize();
  EXPECT_TRUE(manager_->validateOptions(options1_).ok());
  EXPECT_NE(nullptr, manager_->getCache(options1_, dispatcher_));
}

TEST_F(HttpServerPropertiesCacheManagerTest, ValidateOptionsRejectsUnknownKeyValueStoreFactory) {
  setKeyValueStoreConfig(options1_, Protobuf::Struct(), "envoy.key_value.unknown");
  initialize();
  EXPECT_THAT(
      std::string(manager_->validateOptions(options1_).message()),
      testing::HasSubstr("Didn't find a registered implementation for 'envoy.key_value.unknown'"));
}

TEST_F(HttpServerPropertiesCacheManagerTest, ValidateOptionsRejectsInvalidKeyValueStoreConfig) {
  // The store specific configuration nested in the key value store config is checked as well.
  setKeyValueStoreConfig(options1_, testStoreConfig(""));
  initialize();
  const std::string message(manager_->validateOptions(options1_).message());
  EXPECT_THAT(message, testing::HasSubstr("Proto constraint validation failed"));
  EXPECT_THAT(message, testing::HasSubstr("Name"));
}

TEST_F(HttpServerPropertiesCacheManagerTest, ValidateOptionsUsesValidationVisitor) {
  setKeyValueStoreConfig(options1_, testStoreConfig("foo"));
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  initialize(validation_visitor);
  EXPECT_CALL(validation_visitor, runtime()).Times(testing::AtLeast(1));
  EXPECT_TRUE(manager_->validateOptions(options1_).ok());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithKeyValueStoreDoesNotValidate) {
  setKeyValueStoreConfig(options1_, testStoreConfig("foo"));
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  initialize(validation_visitor);
  ASSERT_TRUE(manager_->validateOptions(options1_).ok());
  // Caches are created on worker threads, so creation must not use the manager's validation
  // visitor, which is not thread safe, and the store factory must be told to skip validation.
  EXPECT_CALL(validation_visitor, runtime()).Times(0);
  EXPECT_NE(nullptr, manager_->getCache(options1_, dispatcher_));
  EXPECT_EQ(true, test_store_factory_.last_store_skipped_validation_);
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheForConflictingOptions) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  options2_.set_name(options1_.name());
  // Same as EXPECT_ENVOY_BUG
  EXPECT_DEBUG_DEATH(
      manager_->getCache(options2_, dispatcher_),
      ::testing::ContainsRegex("(?s)options specified alternate protocols cache 'name1' with "
                               "different settings first '.*name: \"name1\""));
}

} // namespace
} // namespace Http
} // namespace Envoy
