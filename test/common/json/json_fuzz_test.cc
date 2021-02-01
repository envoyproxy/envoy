#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

// We have multiple third party JSON parsers in Envoy, both RapidJSON and Protobuf.
// We only fuzz protobuf today, since RapidJSON is deprecated and has known
// limitations when we have deeply nested structures. Do not use RapidJSON for
// anything new in Envoy! See https://github.com/envoyproxy/envoy/issues/4705.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  std::string json_string{reinterpret_cast<const char*>(buf), len};

  // Load via Protobuf JSON parsing, if we can.
  ProtobufWkt::Struct message;
  try {
    MessageUtil::loadFromJson(json_string, message);
    // We should be able to serialize, parse again and get the same result.
    ProtobufWkt::Struct message2;
    MessageUtil::loadFromJson(MessageUtil::getJsonStringFromMessageOrDie(message), message2);
    FUZZ_ASSERT(TestUtility::protoEqual(message, message2));

    // MessageUtil::getYamlStringFromMessage automatically convert types, so we have to do another
    // round-trip.
    std::string yaml = MessageUtil::getYamlStringFromMessage(message);
    ProtobufWkt::Struct yaml_message;
    MessageUtil::loadFromYaml(yaml, yaml_message);

    ProtobufWkt::Struct message3;
    MessageUtil::loadFromYaml(MessageUtil::getYamlStringFromMessage(yaml_message), message3);
    FUZZ_ASSERT(TestUtility::protoEqual(yaml_message, message3));
  } catch (const Envoy::EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Failed due to {}", e.what());
  }
}

} // namespace Fuzz
} // namespace Envoy
