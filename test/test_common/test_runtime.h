// A simple test utility to easily allow for runtime feature overloads in unit tests.
//
// As long as this class is in scope one can do runtime feature overrides:
//
//  TestScopedRuntime scoped_runtime;
//  Runtime::LoaderSingleton::getExisting()->mergeValues(
//      {{"envoy.reloadable_features.test_feature_true", "false"}});
//
//  As long as a TestScopedRuntime exists, Runtime::LoaderSingleton::getExisting()->mergeValues()
//  can safely be called to override runtime values.

#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/runtime/runtime_impl.h"
#include "common/stats/isolated_store_impl.h"

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

    loader_ = std::make_unique<Runtime::ScopedLoaderSingleton>(
        std::make_unique<Runtime::LoaderImpl>(dispatcher_, tls_, config, local_info_, store_,
                                              generator_, validation_visitor_, *api_));
  }

private:
  Event::MockDispatcher dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::IsolatedStoreImpl store_;
  Random::MockRandomGenerator generator_;
  Api::ApiPtr api_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  std::unique_ptr<Runtime::ScopedLoaderSingleton> loader_;
};

} // namespace Envoy
