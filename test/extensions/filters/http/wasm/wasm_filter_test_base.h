#pragma once

#include "envoy/grpc/async_client.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/wasm/wasm_filter.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/router/mocks.h"
#include "test/test_common/wasm_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

using BufferFunction = std::function<void(::Envoy::Buffer::Instance&)>;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::CreateContextFn;
using Envoy::Extensions::Common::Wasm::Plugin;
using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;
using proxy_wasm::ContextBase;
using GrpcService = envoy::config::core::v3::GrpcService;

class TestFilter : public Envoy::Extensions::Common::Wasm::Context {
public:
  TestFilter(Wasm* wasm, uint32_t root_context_id, PluginHandleSharedPtr plugin_handle)
      : Envoy::Extensions::Common::Wasm::Context(wasm, root_context_id, plugin_handle) {}
  MOCK_CONTEXT_LOG_;
};

class TestRoot : public Envoy::Extensions::Common::Wasm::Context {
public:
  TestRoot(Wasm* wasm, const std::shared_ptr<Plugin>& plugin) : Context(wasm, plugin) {}
  MOCK_CONTEXT_LOG_;
};

class WasmHttpFilterTest : public Envoy::Extensions::Common::Wasm::WasmHttpFilterTestBase<
                               testing::TestWithParam<std::tuple<std::string, std::string>>> {
public:
  WasmHttpFilterTest() = default;
  ~WasmHttpFilterTest() override = default;

  CreateContextFn createContextFn() {
    return [](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) -> ContextBase* {
      return new TestRoot(wasm, plugin);
    };
  }

  void setupTest(std::string root_id = "", std::string vm_configuration = "",
                 envoy::extensions::wasm::v3::EnvironmentVariables envs = {}) {
    std::string code;
    if (std::get<0>(GetParam()) == "null") {
      code = "HttpWasmTestCpp";
    } else if (std::get<1>(GetParam()) == "cpp") {
      auto filename = !root_id.empty() ? root_id : vm_configuration;
      if (filename.empty() || filename == "context1" || filename == "context2" ||
          filename == "cluster_metadata") {
        filename = "headers";
      } else if (filename == "metadata" || filename == "property" || filename == "sign" ||
                 filename == "verify_signature" || filename == "sign_and_verify_signature") {
        filename = "advanced";
      } else if (filename == "grpc_call_proto") {
        filename = "grpc_call";
      } else if (filename == "grpc_stream_proto") {
        filename = "grpc_stream";
      }
      code = TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
          absl::StrCat("test/extensions/filters/http/wasm/test_data/", filename, "_cpp.wasm")));
    } else {
      auto filename = !root_id.empty() ? root_id : vm_configuration;
      const auto basic_path = TestEnvironment::runfilesPath(
          absl::StrCat("test/extensions/filters/http/wasm/test_data/", filename));
      code = TestEnvironment::readFileToStringForTest(basic_path + "_rust.wasm");
    }

    setRootId(root_id);
    setEnvs(envs);
    setVmConfiguration(vm_configuration);
    setupBase(std::get<0>(GetParam()), code, createContextFn());
  }

  void setupFilter() { setupFilterBase<TestFilter>(); }

  void setupGrpcStreamTest(Grpc::RawAsyncStreamCallbacks*& callbacks, std::string id);

  TestRoot& rootContext() { return *static_cast<TestRoot*>(root_context_); }
  TestFilter& filter() { return *static_cast<TestFilter*>(context_.get()); }

protected:
  NiceMock<Grpc::MockAsyncStream> async_stream_;
  Grpc::MockAsyncClientManager async_client_manager_;
};

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
