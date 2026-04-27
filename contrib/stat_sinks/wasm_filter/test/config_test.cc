#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/stat_sinks/wasm_filter/v3/wasm_filter.pb.validate.h"
#include "contrib/stat_sinks/wasm_filter/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {
namespace {

TEST(WasmFilterSinkFactoryTest, FactoryRegistered) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
      WasmFilterName);
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.stat_sinks.wasm_filter");
}

TEST(WasmFilterSinkFactoryTest, CreateEmptyConfigProto) {
  WasmFilterSinkFactory factory;
  auto proto = factory.createEmptyConfigProto();
  ASSERT_NE(proto, nullptr);
  EXPECT_NE(
      dynamic_cast<envoy::extensions::stat_sinks::wasm_filter::v3::WasmFilterStatsSinkConfig*>(
          proto.get()),
      nullptr);
}

TEST(WasmFilterSinkFactoryTest, Name) {
  WasmFilterSinkFactory factory;
  EXPECT_EQ(factory.name(), "envoy.stat_sinks.wasm_filter");
}

} // namespace
} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
