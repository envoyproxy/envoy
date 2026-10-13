#pragma once

#include "envoy/network/filter.h"

#include "source/common/protobuf/protobuf.h"

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
  void fuzz(
      const envoy::config::listener::v3::Filter& proto_config,
      const Protobuf::RepeatedPtrField<::test::extensions::filters::network::WriteAction>& actions);
  // Get the name of filters which has been covered by this fuzzer.
  static std::vector<absl::string_view> filterNames();

protected:
  // Set-up filter specific mock expectations in constructor.
  void fuzzerSetup();
  // Reset the states of the mock objects.
  void reset();

private:
  Server::Configuration::FakeFactoryContext factory_context_;
  Event::SimulatedTimeSystem& time_source_;
  Network::WriteFilterSharedPtr write_filter_;
  Network::FilterFactoryCb cb_;
  std::shared_ptr<NiceMock<Network::MockWriteFilterCallbacks>> write_filter_callbacks_;
  std::shared_ptr<NiceMock<Network::MockReadFilterCallbacks>> read_filter_callbacks_;
};

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
