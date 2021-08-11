#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

#include "test/config/utility.h"
#include "test/extensions/filters/http/common/fuzz/filter_fuzz.pb.validate.h"
#include "test/extensions/filters/http/common/fuzz/uber_filter.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

DEFINE_PROTO_FUZZER(const test::extensions::filters::http::FilterFuzzTestCase& input) {
  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::http::FilterFuzzTestCase* input, unsigned int seed) {
        // This ensures that the mutated configs all have valid filter names and type_urls. The list
        // of names and type_urls is pulled from the NamedHttpFilterConfigFactory. All Envoy
        // extensions are built with this test (see BUILD file). This post-processor mutation is
        // applied only when libprotobuf-mutator calls mutate on an input, and *not* during fuzz
        // target execution. Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.
        static const std::vector<absl::string_view> filter_names = Registry::FactoryRegistry<
            Server::Configuration::NamedHttpFilterConfigFactory>::registeredNames();
        static const auto factories = Registry::FactoryRegistry<
            Server::Configuration::NamedHttpFilterConfigFactory>::factories();
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

        // For fuzzing proto data, guide the mutator to useful 'Any' types half
        // the time. The other half the time, let the fuzzing engine choose
        // any message to serialize.
        if (seed % 2 == 0 && input->data().has_proto_body()) {
          UberFilterFuzzer::guideAnyProtoType(input->mutable_data(), seed / 2);
        }
      }};

  try {
    // Catch invalid header characters.
    TestUtility::validate(input);
    ENVOY_LOG_MISC(debug, "Filter configuration: {}", input.config().DebugString());
    // Fuzz filter.
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.data(), input.upstream_data());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
