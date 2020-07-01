#include <vector>

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/network/common/fuzz/network_filter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/uber_filter.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

DEFINE_PROTO_FUZZER(const test::extensions::filters::network::FilterFuzzTestCase& input) {
  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::network::FilterFuzzTestCase* input, unsigned int seed) {
        // This ensures that the mutated configs all have valid filter names and type_urls. The list
        // of names and type_urls is pulled from the NamedNetworkFilterConfigFactory. All Envoy
        // extensions are built with this test (see BUILD file). This post-processor mutation is
        // applied only when libprotobuf-mutator calls mutate on an input, and *not* during fuzz
        // target execution. Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.

        // static const std::vector<absl::string_view> filter_names = Registry::FactoryRegistry<
        //     Server::Configuration::NamedNetworkFilterConfigFactory>::registeredNames();
        static const std::vector<absl::string_view> filter_names = UberFilterFuzzer::filter_names();
        static const auto factories = Registry::FactoryRegistry<
            Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
        // Choose a valid filter name.
        if (std::find(filter_names.begin(), filter_names.end(), input->config().name()) ==
            std::end(filter_names)) {
          absl::string_view filter_name = filter_names[seed % filter_names.size()];
          filter_name = "envoy.filters.network.redis_proxy";
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
    // Fuzz filter.
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy