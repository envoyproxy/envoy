#include "envoy/extensions/access_loggers/wasm/v3/wasm.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/wasm/config.h"
#include "source/extensions/access_loggers/wasm/wasm_access_log_impl.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

class WasmAccessLogConfigTest
    : public testing::TestWithParam<std::tuple<std::string, std::string>> {
protected:
  WasmAccessLogConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(context_, listenerInfo()).WillByDefault(ReturnRef(listener_info_));
    ON_CALL(listener_info_, metadata()).WillByDefault(ReturnRef(listener_metadata_));
    ON_CALL(context_, initManager()).WillByDefault(ReturnRef(init_manager_));
    ON_CALL(context_.server_factory_context_, clusterManager())
        .WillByDefault(ReturnRef(cluster_manager_));
    ON_CALL(context_.server_factory_context_, mainThreadDispatcher())
        .WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(log_stream_info_, requestComplete())
        .WillByDefault(Return(std::chrono::milliseconds(30)));
  }

  void SetUp() override { Envoy::Extensions::Common::Wasm::clearCodeCacheForTesting(); }

  void initializeForRemote() {
    retry_timer_ = new Event::MockTimer();

    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
  Api::ApiPtr api_;
  envoy::config::core::v3::Metadata listener_metadata_;
  Init::ManagerImpl init_manager_{"init_manager"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<StreamInfo::MockStreamInfo> log_stream_info_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmAccessLogConfigTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmAccessLogConfigTest, CreateWasmFromEmpty) {
  auto factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
      "envoy.access_loggers.wasm");
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  AccessLog::FilterPtr filter;

  AccessLog::InstanceSharedPtr instance;
  EXPECT_THROW_WITH_MESSAGE(
      instance = factory->createAccessLogInstance(*message, std::move(filter), context_),
      Common::Wasm::WasmException, "Unable to create Wasm plugin ");
}

TEST_P(WasmAccessLogConfigTest, CreateWasmFromWASM) {
  auto factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
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

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(config, std::move(filter), context_);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<WasmAccessLog*>(instance.get()));
  // Check if the custom stat namespace is registered during the initialization.
  EXPECT_TRUE(api_->customStatNamespaces().registered("wasmcustom"));

  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;
  instance->log({&request_header, &response_header, &response_trailer}, log_stream_info_);

  filter = std::make_unique<NiceMock<AccessLog::MockFilter>>();
  AccessLog::InstanceSharedPtr filter_instance =
      factory->createAccessLogInstance(config, std::move(filter), context_);
  filter_instance->log({&request_header, &response_header, &response_trailer}, log_stream_info_);
}

TEST_P(WasmAccessLogConfigTest, YamlLoadFromFileWasmInvalidConfig) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  auto factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
      "envoy.access_loggers.wasm");
  ASSERT_NE(factory, nullptr);

  const std::string invalid_yaml =
      TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                               std::get<0>(GetParam()), R"EOF("
      configuration:
         "@type": "type.googleapis.com/google.protobuf.StringValue"
         value: "some configuration"
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/access_loggers/wasm/test_data/test_cpp.wasm"
    configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "invalid"
  )EOF"));

  envoy::extensions::access_loggers::wasm::v3::WasmAccessLog proto_config;
  TestUtility::loadFromYaml(invalid_yaml, proto_config);
  EXPECT_THROW_WITH_MESSAGE(factory->createAccessLogInstance(proto_config, nullptr, context_),
                            Envoy::Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin ");
  const std::string valid_yaml =
      TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                               std::get<0>(GetParam()), R"EOF("
      configuration:
         "@type": "type.googleapis.com/google.protobuf.StringValue"
         value: "some configuration"
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/access_loggers/wasm/test_data/test_cpp.wasm"
    configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "valid"
  )EOF"));
  TestUtility::loadFromYaml(valid_yaml, proto_config);
  AccessLog::InstanceSharedPtr filter_instance =
      factory->createAccessLogInstance(proto_config, nullptr, context_);
  filter_instance = factory->createAccessLogInstance(proto_config, nullptr, context_);
  filter_instance->log({}, log_stream_info_);
}

TEST_P(WasmAccessLogConfigTest, YamlLoadFromRemoteWasmCreateFilter) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  const std::string code = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/access_loggers/wasm/test_data/test_cpp.wasm"));
  const std::string sha256 = Hex::encode(
      Envoy::Common::Crypto::UtilitySingleton::get().getSha256Digest(Buffer::OwnedImpl(code)));
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      code:
        remote:
          http_uri:
            uri: https://example.com/data
            cluster: cluster_1
            timeout: 5s
          sha256: )EOF",
                                                                    sha256));
  WasmAccessLogFactory factory;
  envoy::extensions::access_loggers::wasm::v3::WasmAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NiceMock<Http::MockAsyncClient> client;
  NiceMock<Http::MockAsyncClientRequest> request(&client);

  cluster_manager_.initializeThreadLocalClusters({"cluster_1"});
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
  Http::AsyncClient::Callbacks* async_callbacks = nullptr;
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            if (!async_callbacks) {
              async_callbacks = &callbacks;
            }
            return &request;
          }));
  AccessLog::InstanceSharedPtr filter_instance =
      factory.createAccessLogInstance(proto_config, nullptr, context_);
  filter_instance->log({}, log_stream_info_);
  EXPECT_CALL(init_watcher_, ready());
  context_.initManager().initialize(init_watcher_);
  auto response = Http::ResponseMessagePtr{new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}})};
  response->body().add(code);
  async_callbacks->onSuccess(request, std::move(response));
  EXPECT_EQ(context_.initManager().state(), Init::Manager::State::Initialized);
  filter_instance->log({}, log_stream_info_);
}

TEST_P(WasmAccessLogConfigTest, FailedToGetThreadLocalPlugin) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  auto factory = Registry::FactoryRegistry<AccessLog::AccessLogInstanceFactory>::getFactory(
      "envoy.access_loggers.wasm");
  ASSERT_NE(factory, nullptr);

  NiceMock<Envoy::ThreadLocal::MockInstance> threadlocal;
  const std::string yaml = TestEnvironment::substitute(absl::StrCat(R"EOF(
  config:
    vm_config:
      runtime: "envoy.wasm.runtime.)EOF",
                                                                    std::get<0>(GetParam()), R"EOF("
      configuration:
         "@type": "type.googleapis.com/google.protobuf.StringValue"
         value: "some configuration"
      code:
        local:
          filename: "{{ test_rundir }}/test/extensions/access_loggers/wasm/test_data/test_cpp.wasm"
    configuration:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "valid"
  )EOF"));

  envoy::extensions::access_loggers::wasm::v3::WasmAccessLog proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  EXPECT_CALL(context_.server_factory_context_, threadLocal()).WillOnce(ReturnRef(threadlocal));
  threadlocal.registered_ = true;
  AccessLog::InstanceSharedPtr filter_instance =
      factory->createAccessLogInstance(proto_config, nullptr, context_);
  ASSERT_EQ(threadlocal.current_slot_, 1);

  Http::TestRequestHeaderMapImpl request_header;
  Http::TestResponseHeaderMapImpl response_header;
  Http::TestResponseTrailerMapImpl response_trailer;

  filter_instance->log({&request_header, &response_header, &response_trailer}, log_stream_info_);
  // Even if the thread local plugin handle returns nullptr, `log` should not raise error or
  // exception.
  threadlocal.data_[0] = std::make_shared<PluginHandleSharedPtrThreadLocal>(nullptr);
  filter_instance->log({&request_header, &response_header, &response_trailer}, log_stream_info_);
}

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
