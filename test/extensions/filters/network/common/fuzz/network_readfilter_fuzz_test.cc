#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/network/common/fuzz/network_readfilter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/uber_readfilter.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
DEFINE_PROTO_FUZZER(const test::extensions::filters::network::FilterFuzzTestCase& input) {
  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::network::FilterFuzzTestCase* input, unsigned int seed) {
        // This post-processor mutation is applied only when libprotobuf-mutator
        // calls mutate on an input, and *not* during fuzz target execution.
        // Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.

        // TODO(jianwendong): After extending to cover all the filters, we can use
        // `Registry::FactoryRegistry<
        // Server::Configuration::NamedNetworkFilterConfigFactory>::registeredNames()`
        // to get all the filter names instead of calling `UberFilterFuzzer::filter_names()`.
        static const auto filter_names = UberFilterFuzzer::filterNames();
        static const auto factories = Registry::FactoryRegistry<
            Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
        // Choose a valid filter name.
        if (std::find(filter_names.begin(), filter_names.end(), input->config().name()) ==
            std::end(filter_names)) {
          absl::string_view filter_name = filter_names[seed % filter_names.size()];
          input->mutable_config()->set_name(std::string(filter_name));
        }
        // Set the corresponding type_url for Any.
        auto& factory = factories.at(input->config().name());
        input->mutable_config()->mutable_typed_config()->set_type_url(
            absl::StrCat("type.googleapis.com/",
                         factory->createEmptyConfigProto()->GetDescriptor()->full_name()));
      }};

  try {
    TestUtility::validate(input);
    // Check the filter's name in case some filters are not supported yet.
    static const auto filter_names = UberFilterFuzzer::filterNames();
    // TODO(jianwendong): remove this if block after covering all the filters.
    if (std::find(filter_names.begin(), filter_names.end(), input.config().name()) ==
        std::end(filter_names)) {
      ENVOY_LOG_MISC(debug, "Test case with unsupported filter type: {}", input.config().name());
      return;
    }
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy