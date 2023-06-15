#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/network/common/fuzz/network_readfilter_fuzz.pb.validate.h"
#include "test/extensions/filters/network/common/fuzz/uber_readfilter.h"
#include "test/extensions/filters/network/common/fuzz/validated_input_generator_any_map_extensions.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/test_runtime.h"

#include "libprotobuf_mutator/src/libfuzzer/libfuzzer_macro.h"
#include "src/text_format.h" // from libprotobuf_mutator/

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

void ensuredValidFilter(unsigned int seed, envoy::config::listener::v3::Filter* config) {
  // TODO(jianwendong): After extending to cover all the filters, we can use
  // `Registry::FactoryRegistry<
  // Server::Configuration::NamedNetworkFilterConfigFactory>::registeredNames()`
  // to get all the filter names instead of calling `UberFilterFuzzer::filter_names()`.
  static const auto filter_names = UberFilterFuzzer::filterNames();
  static const auto factories = Registry::FactoryRegistry<
      Server::Configuration::NamedNetworkFilterConfigFactory>::factories();

  // Choose a valid filter name.
  if (std::find(filter_names.begin(), filter_names.end(), config->name()) ==
      std::end(filter_names)) {
    absl::string_view filter_name = filter_names[seed % filter_names.size()];
    if (filter_name != config->name()) {
      // Clear old config, or unpacking non-suitable value may crash.
      config->clear_typed_config();
      config->set_name(std::string(filter_name));
    }
  }
  // Set the corresponding type_url for Any.
  auto& factory = factories.at(config->name());
  config->mutable_typed_config()->set_type_url(absl::StrCat(
      "type.googleapis.com/", factory->createEmptyConfigProto()->GetDescriptor()->full_name()));
}

static void TestOneProtoInput(const test::extensions::filters::network::FilterFuzzTestCase& input);

using FuzzerProtoType = test::extensions::filters::network::FilterFuzzTestCase;
// protobuf_mutator::libfuzzer::macro_internal::GetFirstParam<decltype(&TestOneProtoInput)>::type;

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t* data, size_t size, size_t max_size,
                                          unsigned int seed) {
  static unsigned config_mutation_cnt = 0;
  // mutate the config part of the fuzzer only so often.
  static const unsigned config_mutation_limit = 1000;
  static protobuf_mutator::Mutator mutator = [seed] {
    protobuf_mutator::Mutator mutator;
    mutator.Seed(seed);
    return mutator;
  }();
  static ProtobufMessage::ValidatedInputGenerator generator(
      seed, ProtobufMessage::composeFiltersAnyMap(), 20);
  FuzzerProtoType input;
  input.ParseFromString({reinterpret_cast<const char*>(data), size});
  mutator.Mutate(input.mutable_actions(), max_size);

  if (config_mutation_cnt > config_mutation_limit) {
    mutator.Mutate(input.mutable_config(), max_size);
    ensuredValidFilter(seed, input.mutable_config());
    config_mutation_cnt = 0;
  }
  ProtobufMessage::traverseMessage(generator, input, true);
  ++config_mutation_cnt;

  return protobuf_mutator::SaveMessageAsText(input, data, max_size);
}

DEFINE_CUSTOM_PROTO_CROSSOVER_IMPL(false, FuzzerProtoType)
DEFINE_TEST_ONE_PROTO_INPUT_IMPL(false, FuzzerProtoType)
DEFINE_POST_PROCESS_PROTO_MUTATION_IMPL(FuzzerProtoType)
static void TestOneProtoInput(const test::extensions::filters::network::FilterFuzzTestCase& input) {
  TestDeprecatedV2Api _deprecated_v2_api;
  ABSL_ATTRIBUTE_UNUSED static PostProcessorRegistration reg = {
      [](test::extensions::filters::network::FilterFuzzTestCase* input, unsigned int seed) {
        // This post-processor mutation is applied only when libprotobuf-mutator
        // calls mutate on an input, and *not* during fuzz target execution.
        // Replaying a corpus through the fuzzer will not be affected by the
        // post-processor mutation.
        ensuredValidFilter(seed, input->mutable_config());

        ProtobufMessage::ValidatedInputGenerator generator(
            seed, ProtobufMessage::composeFiltersAnyMap(), 20);
        ProtobufMessage::traverseMessage(generator, *input, true);
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
    fuzzer.fuzz(input.config(), input.actions().actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
