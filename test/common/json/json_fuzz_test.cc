#include "common/common/assert.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

// We have multiple third party JSON parsers in Envoy, both RapidJSON and Protobuf.
// Fuzz both RapidJSON and Protobuf JSON loading from a test corpus derived from
// json_loader_test.cc. Ideally we would be doing differential fuzzing and be
// able to compare outputs, e.g. success/failure, recursive traversal of the
// structured objects for equivalence checking. However, even on basic files
// with non-printable ASCII, there are some difference, so we'll need to think a
// bit more about the modulo operator we want to use. For now, at least we get
// crash fuzzing.
// TODO(htuch): maybe only compare structurally on success (but might need to deal
// with RapidJSON integer regression in JsonLoaderTest.Integer?). Maybe also
// do some character set scrubbing before differential comparison.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  std::string json_string{reinterpret_cast<const char*>(buf), len};

  // Load via RapidJSON if we can.
  Json::ObjectSharedPtr object;
  bool rapid_json_ok = false;
  try {
    object = Json::Factory::loadFromString(json_string);
    rapid_json_ok = true;
  } catch (const Envoy::Json::Exception&) {
  }
  // Validate we can re-serialize outside of the exception block, as this should
  // never fail.
  if (rapid_json_ok) {
    object->asJsonString();
  }

  // Load via Protobuf JSON parsing if we can
  ProtobufWkt::Struct message;
  bool protobuf_ok = false;
  try {
    MessageUtil::loadFromJson(json_string, message);
    protobuf_ok = true;
  } catch (const Envoy::EnvoyException&) {
  }
  // Validate we can re-serialize outside of the exception block, as this should
  // never fail.
  if (protobuf_ok) {
    MessageUtil::getJsonStringFromMessage(message);
  }
  if (rapid_json_ok != protobuf_ok) {
    ENVOY_LOG_MISC(debug, "Inconsistent success for JSON parsers: RapidJSON {}, Protobuf {}",
                   rapid_json_ok, protobuf_ok);
  }
  // It would be nice to compare the output, but the two parsers have different
  // behaviors around edge cases, e.g. weird non-printable ASCII sequences. As a
  // result, we leave to future work a true differential fuzzer.
  // FUZZ_ASSERT(rapid_json_ok == protobuf_ok);
}

} // namespace Fuzz
} // namespace Envoy
