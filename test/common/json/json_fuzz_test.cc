#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

static const size_t MaxInputSize = 64 * 1024;

// We have multiple third party JSON parsers in Envoy, nlohmann/JSON, RapidJSON and Protobuf.
// We fuzz nlohmann/JSON and protobuf and compare their results, since RapidJSON is deprecated and
// has known limitations. See https://github.com/envoyproxy/envoy/issues/4705.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  // protect against overly large JSON files
  if (len > MaxInputSize) {
    ENVOY_LOG_MISC(debug, "Buffer length is over {}KiB, skipping test.", MaxInputSize / 1024);
    return;
  }

  std::string json_string{reinterpret_cast<const char*>(buf), len};

  // Load via Protobuf JSON parsing, if we can.
  ProtobufWkt::Struct message;
  try {
    MessageUtil::loadFromJson(json_string, message);
    // We should be able to serialize, parse again and get the same result.
    ProtobufWkt::Struct message2;
    // This can sometimes fail on too deep recursion in case protobuf parsing is configured to have
    // less recursion depth than json parsing in the proto library.
    // This is the only version of MessageUtil::getJsonStringFromMessage function safe to use on
    // untrusted inputs.
    std::string deserialized = MessageUtil::getJsonStringFromMessageOrError(message);
    if (!absl::StartsWith(deserialized, "Failed to convert")) {
      MessageUtil::loadFromJson(deserialized, message2);
      FUZZ_ASSERT(TestUtility::protoEqual(message, message2));
    }

    // MessageUtil::getYamlStringFromMessage automatically convert types, so we have to do another
    // round-trip.
    std::string yaml = MessageUtil::getYamlStringFromMessage(message);
    ProtobufWkt::Struct yaml_message;
    TestUtility::loadFromYaml(yaml, yaml_message);

    ProtobufWkt::Struct message3;
    TestUtility::loadFromYaml(MessageUtil::getYamlStringFromMessage(yaml_message), message3);
    FUZZ_ASSERT(TestUtility::protoEqual(yaml_message, message3));
  } catch (const Envoy::EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Failed due to {}", e.what());
  }
}

} // namespace Fuzz
} // namespace Envoy
