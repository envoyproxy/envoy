#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/network/common/fuzz/network_writefilter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"
#include "test/extensions/filters/network/common/fuzz/validated_input_generator_any_map_extensions.h"
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

        // TODO(jianwendong): consider using a factory to store the names of all
        // writeFilters.
        static const auto filter_names = UberWriteFilterFuzzer::filterNames();
        static const auto factories = Registry::FactoryRegistry<
            Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
        // Choose a valid filter name.
        if (std::find(filter_names.begin(), filter_names.end(), input->config().name()) ==
            std::end(filter_names)) {
          absl::string_view filter_name = filter_names[seed % filter_names.size()];
          if (filter_name != input->config().name()) {
            // Clear old config, or unpacking non-suitable value may crash.
            input->mutable_config()->clear_typed_config();
            input->mutable_config()->set_name(std::string(filter_name));
          }
        }
        // Set the corresponding type_url for Any.
        auto& factory = factories.at(input->config().name());
        input->mutable_config()->mutable_typed_config()->set_type_url(
            absl::StrCat("type.googleapis.com/",
                         factory->createEmptyConfigProto()->GetDescriptor()->full_name()));
        ProtobufMessage::ValidatedInputGenerator generator(
            seed, ProtobufMessage::composeFiltersAnyMap(), 20);
        ProtobufMessage::traverseMessage(generator, *input, true);
      }};
  try {
    TestUtility::validate(input);
    // Check the filter's name in case some filters are not supported yet.
    // TODO(jianwendong): remove this if block when we have a factory for writeFilters.
    static const auto filter_names = UberWriteFilterFuzzer::filterNames();
    if (std::find(filter_names.begin(), filter_names.end(), input.config().name()) ==
        std::end(filter_names)) {
      ENVOY_LOG_MISC(debug, "Test case with unsupported filter type: {}", input.config().name());
      return;
    }
    static UberWriteFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
