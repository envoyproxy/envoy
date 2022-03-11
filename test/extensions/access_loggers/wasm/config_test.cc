#include "envoy/extensions/access_loggers/wasm/v3/wasm.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/wasm/config.h"
#include "source/extensions/access_loggers/wasm/wasm_access_log_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

class TestFactoryContext : public NiceMock<Server::Configuration::MockServerFactoryContext> {
public:
  TestFactoryContext(Api::Api& api, Stats::Scope& scope) : api_(api), scope_(scope) {}
  Api::Api& api() override { return api_; }
  Stats::Scope& scope() override { return scope_; }

private:
  Api::Api& api_;
  Stats::Scope& scope_;
};

class WasmAccessLogConfigTest
    : public testing::TestWithParam<std::tuple<std::string, std::string>> {};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmAccessLogConfigTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmAccessLogConfigTest, CreateWasmFromEmpty) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::AccessLogInstanceFactory>::getFactory(
          "envoy.access_loggers.wasm");
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  AccessLog::InstanceSharedPtr instance;
  EXPECT_THROW_WITH_MESSAGE(
      instance = factory->createAccessLogInstance(*message, std::move(filter), context),
      Common::Wasm::WasmException, "Unable to create Wasm access log ");
}

TEST_P(WasmAccessLogConfigTest, CreateWasmFromWASM) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::AccessLogInstanceFactory>::getFactory(
          "envoy.access_loggers.wasm");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::access_loggers::wasm::v3::WasmAccessLog config;
  config.mutable_config()->mutable_vm_config()->set_runtime(
      absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam())));
  std::string code;
  if (std::get<0>(GetParam()) != "null") {
    code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/extensions/access_loggers/wasm/test_data/test_cpp.wasm"));
  } else {
    code = "AccessLoggerTestCpp";
  }
  config.mutable_config()->mutable_vm_config()->mutable_code()->mutable_local()->set_inline_bytes(
      code);
  // Test Any configuration.
  ProtobufWkt::Struct some_proto;
  config.mutable_config()->mutable_vm_config()->mutable_configuration()->PackFrom(some_proto);

  AccessLog::FilterPtr filter;
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  TestFactoryContext context(*api, stats_store);

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(config, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<WasmAccessLog*>(instance.get()));
  // Check if the custom stat namespace is registered during the initialization.
  EXPECT_TRUE(api->customStatNamespaces().registered("wasmcustom"));

  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  StreamInfo::MockStreamInfo log_stream_info;
  instance->log(&request_header, &response_header, &response_trailer, log_stream_info);

  filter = std::make_unique<NiceMock<AccessLog::MockFilter>>();
  AccessLog::InstanceSharedPtr filter_instance =
      factory->createAccessLogInstance(config, std::move(filter), context);
  filter_instance->log(&request_header, &response_header, &response_trailer, log_stream_info);
}

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
