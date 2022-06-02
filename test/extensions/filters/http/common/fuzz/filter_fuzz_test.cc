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

  // The filters to exclude in this general fuzzer. For some of them dedicated fuzzer
  // exists. See #20737 or #21141.
  static const absl::flat_hash_set<absl::string_view> exclusion_list = {
      "envoy.ext_proc",
      "envoy.filters.http.alternate_protocols_cache",
      "envoy.filters.http.composite",
      "envoy.filters.http.ext_proc",
      "envoy.ext_authz",
      "envoy.filters.http.ext_authz"};

  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::http::FilterFuzzTestCase* input, unsigned int seed) {
        // This ensures that the mutated configs all have valid filter names and type_urls. The list
        // of names and type_urls is pulled from the NamedHttpFilterConfigFactory. All Envoy
        // extensions are built with this test (see BUILD file). This post-processor mutation is
        // applied only when libprotobuf-mutator calls mutate on an input, and *not* during fuzz
        // target execution. Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.
        static const std::vector<absl::string_view> registered_names = Registry::FactoryRegistry<
            Server::Configuration::NamedHttpFilterConfigFactory>::registeredNames();
        // Exclude filters that don't work with this test. See #20737
        std::vector<absl::string_view> filter_names;
        filter_names.reserve(registered_names.size());
        std::for_each(registered_names.begin(), registered_names.end(),
                      [&](const absl::string_view& filter) {
                        if (!exclusion_list.contains(filter)) {
                          filter_names.push_back(filter);
                        }
                      });
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
    // Somehow (probably before the mutator was extended by a filter) the fuzzer picked up the
    // extensions it can not fuzz (like the ext_* ones). Therefore reject these here.
    if (exclusion_list.contains(input.config().name())) {
      ENVOY_LOG_MISC(debug, "Filter {} not supported.", input.config().name());
      return;
    }
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
