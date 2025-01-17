#include "envoy/api/os_sys_calls.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.validate.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.validate.h"

#include "source/extensions/config_subscription/grpc/xds_source_id.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/resources.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/config/source/kv_store_xds_delegate.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

using ::Envoy::Config::DecodedResourceRef;
using ::Envoy::Config::XdsConfigSourceId;
using ::Envoy::Config::XdsSourceId;

envoy::config::core::v3::TypedExtensionConfig kvStoreDelegateConfig() {
  const std::string filename = TestEnvironment::temporaryPath("xds_kv_store.txt");
  Api::OsSysCallsSingleton().get().unlink(filename.c_str());

  const std::string config_str = fmt::format(R"EOF(
    name: envoy.config.config.KeyValueStoreXdsDelegate
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.config.v3alpha.KeyValueStoreXdsDelegateConfig
      key_value_store_config:
        config:
          name: envoy.key_value.file_based
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
            filename: {}
    )EOF",
                                             filename);

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(config_str, config);
  return config;
}

class KeyValueStoreXdsDelegateTest : public testing::Test {
public:
  KeyValueStoreXdsDelegateTest() : api_(Api::createApiForTest(store_)) {
    auto config = kvStoreDelegateConfig();
    Extensions::Config::KeyValueStoreXdsDelegateFactory delegate_factory;
    xds_delegate_ = delegate_factory.createXdsResourcesDelegate(
        config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *api_, dispatcher_);
  }

protected:
  envoy::service::runtime::v3::Runtime parseYamlIntoRuntimeResource(const std::string& yaml) {
    envoy::service::runtime::v3::Runtime runtime;
    TestUtility::loadFromYaml(yaml, runtime);
    return runtime;
  }

  envoy::config::cluster::v3::Cluster parseYamlIntoClusterResource(const std::string& yaml) {
    envoy::config::cluster::v3::Cluster cluster;
    TestUtility::loadFromYaml(yaml, cluster);
    return cluster;
  }

  template <typename Resource>
  void checkSavedResources(const XdsSourceId& source_id,
                           const absl::flat_hash_set<std::string>& resource_names,
                           const std::vector<DecodedResourceRef>& expected_resources) {
    // Retrieve the xDS resources.
    const auto retrieved_resources = xds_delegate_->getResources(source_id, resource_names);
    // Check that they're the same.
    EXPECT_EQ(expected_resources.size(), retrieved_resources.size());
    for (size_t i = 0; i < expected_resources.size(); ++i) {
      Resource unpacked_resource;
      THROW_IF_NOT_OK(MessageUtil::unpackTo(retrieved_resources[i].resource(), unpacked_resource));
      TestUtility::protoEqual(expected_resources[i].get().resource(), unpacked_resource);
    }
  }

  Stats::TestUtil::TestStore store_;
  Api::ApiPtr api_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  Config::XdsResourcesDelegatePtr xds_delegate_;
  Event::SimulatedTimeSystem time_source_;
};

TEST_F(KeyValueStoreXdsDelegateTest, SaveAndRetrieve) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  const auto saved_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2});
  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};
  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"some_resource_1", "some_resource_2"},
      saved_resources.refvec_);
  EXPECT_EQ(1, store_.counter("xds.kv_store.load_success").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resources_not_found").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resource_missing").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.parse_failed").value());
}

TEST_F(KeyValueStoreXdsDelegateTest, MultipleAuthoritiesAndTypes) {
  const std::string authority_1 = "rtds_cluster";
  const std::string authority_2 = "127.0.0.1:8585";

  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  auto cluster_resource_1 = parseYamlIntoClusterResource(R"EOF(
    name: cluster_1
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF");

  const auto authority_1_runtime_resources = TestUtility::decodeResources({runtime_resource_1});
  const auto authority_2_runtime_resources = TestUtility::decodeResources({runtime_resource_2});
  const auto authority_2_cluster_resources = TestUtility::decodeResources({cluster_resource_1});

  const XdsConfigSourceId source_id_1{authority_1, Config::TypeUrl::get().Runtime};
  const XdsConfigSourceId source_id_2_runtime{authority_2, Config::TypeUrl::get().Runtime};
  const XdsConfigSourceId source_id_2_cluster{authority_2, Config::TypeUrl::get().Cluster};

  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id_1, authority_1_runtime_resources.refvec_);
  xds_delegate_->onConfigUpdated(source_id_2_runtime, authority_2_runtime_resources.refvec_);
  xds_delegate_->onConfigUpdated(source_id_2_cluster, authority_2_cluster_resources.refvec_);

  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id_1, /*resource_names=*/{"some_resource_1"}, authority_1_runtime_resources.refvec_);
  EXPECT_EQ(1, store_.counter("xds.kv_store.load_success").value());
  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id_2_runtime,
                                                            /*resource_names=*/{"some_resource_2"},
                                                            authority_2_runtime_resources.refvec_);
  EXPECT_EQ(2, store_.counter("xds.kv_store.load_success").value());
  checkSavedResources<envoy::config::cluster::v3::Cluster>(
      source_id_2_cluster, /*resource_names=*/{"cluster_1"}, authority_2_cluster_resources.refvec_);
  EXPECT_EQ(3, store_.counter("xds.kv_store.load_success").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resources_not_found").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resource_missing").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.parse_failed").value());
}

TEST_F(KeyValueStoreXdsDelegateTest, UpdatedSotwResources) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");

  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};

  // Save xDS resources.
  const auto saved_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2});
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  // Update xDS resources.
  runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: klm
  )EOF");
  auto runtime_resource_3 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_3
    layer:
      xyz: 123
  )EOF");
  const auto updated_saved_resources = TestUtility::decodeResources({runtime_resource_3});
  xds_delegate_->onConfigUpdated(source_id, updated_saved_resources.refvec_);

  // Make sure all resources are present and at their latest versions.
  const auto all_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2, runtime_resource_3});
  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"some_resource_1", "some_resource_2", "some_resource_3"},
      all_resources.refvec_);
  EXPECT_EQ(1, store_.counter("xds.kv_store.load_success").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resources_not_found").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resource_missing").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.parse_failed").value());
}

TEST_F(KeyValueStoreXdsDelegateTest, Wildcard) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  const auto saved_resources =
      TestUtility::decodeResources({runtime_resource_1, runtime_resource_2});
  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};
  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  // Empty resource names, or just one entry with "*" means wildcard.
  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id, /*resource_names=*/{},
                                                            saved_resources.refvec_);
  EXPECT_EQ(1, store_.counter("xds.kv_store.load_success").value());
  checkSavedResources<envoy::service::runtime::v3::Runtime>(source_id, /*resource_names=*/{"*"},
                                                            saved_resources.refvec_);
  EXPECT_EQ(2, store_.counter("xds.kv_store.load_success").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resources_not_found").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.resource_missing").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.parse_failed").value());
}

TEST_F(KeyValueStoreXdsDelegateTest, ResourceNotFound) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  const auto saved_resources = TestUtility::decodeResources({runtime_resource_1});
  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};
  // Save xDS resources.
  xds_delegate_->onConfigUpdated(source_id, saved_resources.refvec_);

  // Empty resource names, or just one entry with "*" means wildcard.
  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"non_existent"}, /*expected_resources=*/{});
  EXPECT_EQ(0, store_.counter("xds.kv_store.load_success").value());
  EXPECT_EQ(1, store_.counter("xds.kv_store.resources_not_found").value());
  EXPECT_EQ(1, store_.counter("xds.kv_store.resource_missing").value());
  EXPECT_EQ(0, store_.counter("xds.kv_store.parse_failed").value());
}

TEST_F(KeyValueStoreXdsDelegateTest, ResourcesWithTTL) {
  const std::string authority_1 = "rtds_cluster";
  auto runtime_resource_1 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_1
    layer:
      foo: bar
      baz: meh
  )EOF");
  auto runtime_resource_2 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_2
    layer:
      abc: xyz
  )EOF");
  auto runtime_resource_3 = parseYamlIntoRuntimeResource(R"EOF(
    name: some_resource_3
    layer:
      boo: yikes
  )EOF");

  // some_resource_1 has no TTL
  // some_resource_2 has a TTL of 30 seconds.
  // some_resource_3 has a TTL of 60 seconds.
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
  auto* resource = resources.Add();
  resource->set_name("some_resource_1");
  resource->mutable_resource()->PackFrom(runtime_resource_1);
  resource = resources.Add();
  resource->set_name("some_resource_2");
  resource->mutable_resource()->PackFrom(runtime_resource_2);
  resource->mutable_ttl()->set_seconds(30);
  resource = resources.Add();
  resource->set_name("some_resource_3");
  resource->mutable_resource()->PackFrom(runtime_resource_3);
  resource->mutable_ttl()->set_seconds(60);

  auto decoded_resources = TestUtility::decodeResources<envoy::service::runtime::v3::Runtime>(
      resources, /*version=*/"1");

  // Save xDS resources.
  const XdsConfigSourceId source_id{authority_1, Config::TypeUrl::get().Runtime};
  xds_delegate_->onConfigUpdated(source_id, decoded_resources.refvec_);

  // TTL hasn't expired, so we should have all three xDS resources.
  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"some_resource_1", "some_resource_2", "some_resource_3"},
      decoded_resources.refvec_);

  // Advance time past the first TTL and let the timers fire.
  time_source_.advanceTimeWait(std::chrono::seconds(45));

  // We should only have resources 1 and 3.
  decoded_resources.refvec_.erase(std::next(decoded_resources.refvec_.begin())); // delete 2nd entry
  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"some_resource_1", "some_resource_3"},
      decoded_resources.refvec_);

  // Advance time past the second TTL and let the timers fire.
  time_source_.advanceTimeWait(std::chrono::seconds(45));

  // We should only have resource 1.
  decoded_resources.refvec_.erase(std::next(decoded_resources.refvec_.begin())); // delete 2nd entry
  checkSavedResources<envoy::service::runtime::v3::Runtime>(
      source_id, /*resource_names=*/{"some_resource_1"}, decoded_resources.refvec_);
}

} // namespace
} // namespace Envoy
