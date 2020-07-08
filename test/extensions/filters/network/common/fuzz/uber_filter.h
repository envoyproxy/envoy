#include "envoy/network/filter.h"

#include "common/protobuf/protobuf.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer();
  // This creates the filter config and runs the fuzzed data against the filter.
  void
  fuzz(const envoy::config::listener::v3::Filter& proto_config,
       const Protobuf::RepeatedPtrField<::test::extensions::filters::network::Action>& actions);
  // Get the name of filters which has been covered by this fuzzer.
  static std::vector<absl::string_view> filter_names();
  // Avoid issues in destructors.
  void reset(const std::string filter_name);
  void perFilterSetup(const std::string filter_name);

protected:
  // Set-up filter specific mock expectations in constructor.
  void fuzzerSetup();
  // Set-up mock expectations each timer when a filter is fuzzed.
  void filterSetup(const envoy::config::listener::v3::Filter& proto_config);

private:
  Server::Configuration::MockFactoryContext factory_context_;
  Network::ReadFilterSharedPtr read_filter_;
  Network::FilterFactoryCb cb_;
  Network::Address::InstanceConstSharedPtr addr_;
  Upstream::MockClusterManager cluster_manager_;
  Event::SimulatedTimeSystem time_source_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Runtime::MockLoader runtime_;
  std::shared_ptr<NiceMock<Network::MockReadFilterCallbacks>> read_filter_callbacks_;
  std::unique_ptr<Grpc::MockAsyncRequest> async_request_;
  std::unique_ptr<Grpc::MockAsyncClient> async_client_;
  std::unique_ptr<Grpc::MockAsyncClientFactory> async_client_factory_;
  Tracing::MockSpan span_;
};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
