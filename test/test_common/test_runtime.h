#pragma once

// A simple test utility to easily allow for runtime feature overloads in unit tests.
//
// As long as this class is in scope one can do runtime feature overrides:
//
//  TestScopedRuntime scoped_runtime;
//  scoped_runtime.mergeValues(
//      {{"envoy.reloadable_features.test_feature_true", "false"}});

#pragma once

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

    Runtime::LoaderPtr runtime_ptr = std::make_unique<Runtime::LoaderImpl>(
        dispatcher_, tls_, config, local_info_, store_, generator_, validation_visitor_, *api_);
    // This will ignore values set in test, but just use flag defaults!
    runtime_ = std::move(runtime_ptr);
  }

  Runtime::Loader& loader() { return *runtime_; }

  void mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
    loader().mergeValues(values);
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
    loader().mergeValues({
        {"envoy.test_only.broken_in_production.enable_deprecated_v2_api", "true"},
        {"envoy.features.enable_all_deprecated_features", "true"},
    });
  }
};

} // namespace Envoy
