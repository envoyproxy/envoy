#pragma once

// A simple test utility to easily allow for runtime feature overloads in unit tests.
//
// As long as this class is in scope one can do runtime feature overrides:
//
//  TestScopedRuntime scoped_runtime;
//  scoped_runtime.mergeValues(
//      {{"envoy.reloadable_features.test_feature_true", "false"}});
//
// TestScopedRuntime depends on the admin interface being compiled into the binary.
// For build options where the admin interface is not available (particularly, Envoy Mobile), use
// TestScopedStaticReloadableFeaturesRuntime. As the name suggests, it only works with reloadable
// features:
//
//  TestScopedStaticReloadableFeaturesRuntime scoped_runtime(
//    {{"dfp_mixed_cache", false}, {"always_use_v6", true}});
//
// This will translate to envoy.reloadable_features.dfp_mixed_cache being set to false and
// envoy.reloadable_features.always_use_v6 being set to true in the static runtime layer.

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/runtime/runtime_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {

class TestScopedRuntime {
public:
  TestScopedRuntime() : api_(Api::createApiForTest()) {
    envoy::config::bootstrap::v3::LayeredRuntime config;
    // The existence of an admin layer is required for mergeValues() to work.
    config.add_layers()->mutable_admin_layer();

    absl::StatusOr<std::unique_ptr<Runtime::LoaderImpl>> loader = Runtime::LoaderImpl::create(
        dispatcher_, tls_, config, local_info_, store_, generator_, validation_visitor_, *api_);
    THROW_IF_NOT_OK(loader.status());
    // This will ignore values set in test, but just use flag defaults!
    runtime_ = std::move(loader.value());
  }

  Runtime::Loader& loader() { return *runtime_; }

  void mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
    THROW_IF_NOT_OK(loader().mergeValues(values));
  }

protected:
  absl::FlagSaver saver_;
  Event::MockDispatcher dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::TestUtil::TestStore store_;
  Random::MockRandomGenerator generator_;
  Api::ApiPtr api_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  std::unique_ptr<Runtime::Loader> runtime_;
};

class TestScopedStaticReloadableFeaturesRuntime {
public:
  TestScopedStaticReloadableFeaturesRuntime(const std::vector<std::pair<std::string, bool>>& values)
      : api_(Api::createApiForTest()) {
    envoy::config::bootstrap::v3::LayeredRuntime config;
    // Set up runtime.
    auto* runtime = config.add_layers();
    runtime->set_name("test_static_layer_test_runtime");
    ProtobufWkt::Struct envoy_layer;
    ProtobufWkt::Struct& runtime_values =
        *(*envoy_layer.mutable_fields())["envoy"].mutable_struct_value();
    ProtobufWkt::Struct& flags =
        *(*runtime_values.mutable_fields())["reloadable_features"].mutable_struct_value();
    for (const auto& [key, value] : values) {
      (*flags.mutable_fields())[key].set_bool_value(value);
    }
    runtime->mutable_static_layer()->MergeFrom(envoy_layer);

    absl::StatusOr<std::unique_ptr<Runtime::LoaderImpl>> loader = Runtime::LoaderImpl::create(
        dispatcher_, tls_, config, local_info_, store_, generator_, validation_visitor_, *api_);
    THROW_IF_NOT_OK(loader.status());
    // This will ignore values set in test, but just use flag defaults!
    runtime_ = std::move(loader.value());
  }

protected:
  absl::FlagSaver saver_;
  Event::MockDispatcher dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::TestUtil::TestStore store_;
  Random::MockRandomGenerator generator_;
  Api::ApiPtr api_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  std::unique_ptr<Runtime::Loader> runtime_;
};

class TestDeprecatedV2Api : public TestScopedRuntime {
public:
  TestDeprecatedV2Api() { allowDeprecatedV2(); }
  void allowDeprecatedV2() {
    THROW_IF_NOT_OK(loader().mergeValues({
        {"envoy.test_only.broken_in_production.enable_deprecated_v2_api", "true"},
        {"envoy.features.enable_all_deprecated_features", "true"},
    }));
  }
};

} // namespace Envoy
