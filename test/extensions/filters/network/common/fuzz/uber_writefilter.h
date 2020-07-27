#include "envoy/network/filter.h"

#include "common/protobuf/protobuf.h"

#include "test/extensions/filters/network/common/fuzz/network_writefilter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/utils/fakes.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

class UberWriteFilterFuzzer {
public:
  UberWriteFilterFuzzer();
  // This creates the filter config and runs the fuzzed data against the filter.
  void
  fuzz(const envoy::config::listener::v3::Filter& proto_config,
       const Protobuf::RepeatedPtrField<::test::extensions::filters::network::WriteAction>& actions);
  // Get the name of filters which has been covered by this fuzzer.
  static std::vector<absl::string_view> filterNames();
  // Check whether the filter's config is invalid for fuzzer(e.g. system call).
  void checkInvalidInputForFuzzer(const std::string& filter_name,
                                  Protobuf::Message* config_message);

protected:
  // Set-up filter specific mock expectations in constructor.
  void fuzzerSetup();
  // Reset the states of the mock objects.
  void reset();
  // Mock behaviors for specific filters.
  void perFilterSetup(const std::string& filter_name);

private:
  Server::Configuration::FakeFactoryContext factory_context_;
  Network::WriteFilterSharedPtr write_filter_;
  Network::FilterFactoryCb cb_;
  // Network::Address::InstanceConstSharedPtr pipe_addr_;
  // Network::Address::InstanceConstSharedPtr ipv4_addr_;
  // Event::SimulatedTimeSystem& time_source_;
  std::shared_ptr<NiceMock<Network::MockWriteFilterCallbacks>> write_filter_callbacks_;
  std::shared_ptr<NiceMock<Network::MockReadFilterCallbacks>> read_filter_callbacks_;
  // NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  // std::unique_ptr<Grpc::MockAsyncRequest> async_request_;
  // std::unique_ptr<Grpc::MockAsyncClient> async_client_;
  // std::unique_ptr<Grpc::MockAsyncClientFactory> async_client_factory_;
  // Tracing::MockSpan span_;
};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
