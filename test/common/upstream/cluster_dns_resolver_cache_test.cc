#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/upstream/cluster_dns_resolver_cache.h"

#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

class ClusterDnsResolverCacheTest : public testing::Test {
protected:
  ClusterDnsResolverCacheTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  // `qcache_max_ttl` is only used here as a convenient way to vary the config hash.
  envoy::config::core::v3::TypedExtensionConfig caresConfig(uint32_t qcache_max_ttl) {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
    cares.mutable_qcache_max_ttl()->set_value(qcache_max_ttl);

    envoy::config::core::v3::TypedExtensionConfig typed_config;
    std::ignore = typed_config.mutable_typed_config()->PackFrom(cares);
    typed_config.set_name(std::string(Network::CaresDnsResolver));
    return typed_config;
  }

  envoy::config::core::v3::TypedExtensionConfig getAddrInfoConfig() {
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig config;

    envoy::config::core::v3::TypedExtensionConfig typed_config;
    std::ignore = typed_config.mutable_typed_config()->PackFrom(config);
    typed_config.set_name("envoy.network.dns_resolver.getaddrinfo");
    return typed_config;
  }

  Network::DnsResolverSharedPtr
  getOrCreate(const envoy::config::core::v3::TypedExtensionConfig& config) {
    Network::DnsResolverFactory& factory = Network::createDnsResolverFactoryFromTypedConfig(config);
    auto resolver_or_error = cache_.getOrCreate(factory, *dispatcher_, *api_, config);
    EXPECT_TRUE(resolver_or_error.ok());
    return resolver_or_error.value();
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  ClusterDnsResolverCache cache_;
};

// The point of the cache: clusters configured identically share one resolver, and with it the
// resolver's query cache.
TEST_F(ClusterDnsResolverCacheTest, SharesResolverForIdenticalConfig) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.shared_cares_dns_resolver", "true"}});

  auto config = caresConfig(11);

  auto resolver1 = getOrCreate(config);
  auto resolver2 = getOrCreate(config);

  EXPECT_NE(nullptr, resolver1);
  EXPECT_EQ(resolver1.get(), resolver2.get());
}

TEST_F(ClusterDnsResolverCacheTest, DoesNotShareResolverForDifferentConfig) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.shared_cares_dns_resolver", "true"}});

  auto resolver1 = getOrCreate(caresConfig(22));
  auto resolver2 = getOrCreate(caresConfig(33));

  EXPECT_NE(resolver1.get(), resolver2.get());
}

TEST_F(ClusterDnsResolverCacheTest, DoesNotShareWhenRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.shared_cares_dns_resolver", "false"}});

  auto config = caresConfig(44);

  auto resolver1 = getOrCreate(config);
  auto resolver2 = getOrCreate(config);

  EXPECT_NE(resolver1.get(), resolver2.get());
}

// Sharing is deliberately limited to c-ares. Other resolver types keep one resolver per cluster.
TEST_F(ClusterDnsResolverCacheTest, DoesNotShareNonCaresResolver) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.shared_cares_dns_resolver", "true"}});

  auto config = getAddrInfoConfig();

  auto resolver1 = getOrCreate(config);
  auto resolver2 = getOrCreate(config);

  EXPECT_NE(nullptr, resolver1);
  EXPECT_NE(resolver1.get(), resolver2.get());
}

// Entries are weak, so once every cluster using a resolver is gone the cache creates a new one
// rather than handing back a dangling pointer.
TEST_F(ClusterDnsResolverCacheTest, CreatesNewResolverAfterCachedOneIsReleased) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.shared_cares_dns_resolver", "true"}});

  auto config = caresConfig(55);

  {
    auto released = getOrCreate(config);
  }

  auto resolver = getOrCreate(config);
  EXPECT_NE(nullptr, resolver);

  // And the fresh one is itself cached.
  auto shared = getOrCreate(config);
  EXPECT_EQ(resolver.get(), shared.get());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
