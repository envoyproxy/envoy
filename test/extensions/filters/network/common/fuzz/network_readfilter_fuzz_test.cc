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

static void TestOneProtoInput(const test::extensions::filters::network::FilterFuzzTestCase&);
using FuzzerProtoType = test::extensions::filters::network::FilterFuzzTestCase;

extern "C" size_t LLVMFuzzerCustomMutator(uint8_t* data, size_t size, size_t max_size,
                                          unsigned int seed) {
  // mutate the config part of the fuzzer only with a probability of
  static const unsigned config_mutation_probability = 1 /* / 100 */;
  static protobuf_mutator::Mutator mutator = [seed] {
    protobuf_mutator::Mutator mutator;
    mutator.Seed(seed);
    return mutator;
  }();
  static ProtobufMessage::ValidatedInputGenerator generator(
      seed, ProtobufMessage::composeFiltersAnyMap(), 20);
  static Random::PsuedoRandomGenerator64 random;
  static bool _random_inited = [seed] {
    random.initializeSeed(seed);
    return true;
  }();
  _random_inited = !_random_inited;

  FuzzerProtoType input;
  input.ParseFromString({reinterpret_cast<const char*>(data), size});
  if (random.random() % 100 < config_mutation_probability) {
    test::extensions::filters::network::FuzzHelperForActions actions;
    *actions.mutable_actions() = *input.mutable_actions();
    mutator.Mutate(&actions, max_size);
  } else {
    mutator.Mutate(input.mutable_config(), max_size);
    ensuredValidFilter(seed, input.mutable_config());
  }
  ProtobufMessage::traverseMessage(generator, input, true);

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
    fuzzer.fuzz(input.config(), input.actions());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
