#pragma once

#include "source/common/stats/custom_stat_namespaces_impl.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/fuzz/utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

// Generic filter fuzzer that can fuzz any HttpFilter.
class UberFilterFuzzer : public HttpFilterFuzzer {
public:
  UberFilterFuzzer();

  // This creates the filter config and runs the fuzzed data against the filter.
  void fuzz(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                proto_config,
            const test::fuzz::HttpData& downstream_data, const test::fuzz::HttpData& upstream_data);

  // For fuzzing proto data, guide the mutator to useful 'Any' types.
  static void guideAnyProtoType(test::fuzz::HttpData* mutable_data, uint choice);

  void reset();

protected:
  // Set-up filter specific mock expectations in constructor.
  void perFilterSetup();
  // Filter specific input cleanup.
  void cleanFuzzedConfig(absl::string_view filter_name, Protobuf::Message* message);

private:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Network::MockListenerInfo> listener_info_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  Http::FilterFactoryCb cb_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Http::MockAsyncClientRequest> async_request_;
  envoy::config::core::v3::Metadata listener_metadata_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  TestScopedRuntime scoped_runtime_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;

  // Filter constructed from the config.
  Http::StreamDecoderFilterSharedPtr decoder_filter_;
  Http::StreamEncoderFilterSharedPtr encoder_filter_;
  AccessLog::InstanceSharedPtr access_logger_;

  // Mocked callbacks.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  Api::MockApi api_{};
  Thread::ThreadFactory& thread_factory_;
  Event::DispatcherPtr worker_thread_dispatcher_;
  std::function<void()> destroy_filters_ = []() {};

  const Buffer::Instance* decoding_buffer_{};
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
