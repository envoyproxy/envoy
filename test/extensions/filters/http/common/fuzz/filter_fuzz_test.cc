#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

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
          // These types are request/response from the test Bookstore service
          // for the gRPC Transcoding filter.
          static const std::vector<std::string> expected_types = {
              "type.googleapis.com/bookstore.ListShelvesResponse",
              "type.googleapis.com/bookstore.CreateShelfRequest",
              "type.googleapis.com/bookstore.GetShelfRequest",
              "type.googleapis.com/bookstore.DeleteShelfRequest",
              "type.googleapis.com/bookstore.ListBooksRequest",
              "type.googleapis.com/bookstore.CreateBookRequest",
              "type.googleapis.com/bookstore.GetBookRequest",
              "type.googleapis.com/bookstore.UpdateBookRequest",
              "type.googleapis.com/bookstore.DeleteBookRequest",
              "type.googleapis.com/bookstore.GetAuthorRequest",
              "type.googleapis.com/bookstore.EchoBodyRequest",
              "type.googleapis.com/bookstore.EchoStructReqResp",
              "type.googleapis.com/bookstore.Shelf",
              "type.googleapis.com/bookstore.Book",
              "type.googleapis.com/google.protobuf.Empty",
              "type.googleapis.com/google.api.HttpBody",
          };
          ProtobufWkt::Any* mutable_any =
              input->mutable_data()->mutable_proto_body()->mutable_message();
          const std::string& type_url = expected_types[(seed / 2) % expected_types.size()];
          mutable_any->set_type_url(type_url);
        }
      }};

  try {
    // Catch invalid header characters.
    TestUtility::validate(input);
    // Fuzz filter.
    static UberFilterFuzzer fuzzer;
    fuzzer.fuzz(input.config(), input.data());
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
  }
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
