// NOLINT(namespace-envoy)
#include <google/protobuf/util/json_util.h>

#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics_full.h"
// Required Proxy-Wasm ABI version.
extern "C" PROXY_WASM_KEEPALIVE void proxy_abi_version_0_1_0() {}
#else
#include "envoy/config/core/v3/grpc_service.pb.h"
using envoy::config::core::v3::GrpcService;
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(WasmSpeedCpp)

int xDoNotRemove = 0;

google::protobuf::Arena arena;

google::protobuf::Struct args;
google::protobuf::Struct* args_arena =
    google::protobuf::Arena::CreateMessage<google::protobuf::Struct>(&arena);
std::string configuration = R"EOF(
  {
    "NAME":"test_pod",
    "NAMESPACE":"test_namespace",
    "LABELS": {
        "app": "productpage",
        "version": "v1",
        "pod-template-hash": "84975bc778"
    },
    "OWNER":"test_owner",
    "WORKLOAD_NAME":"test_workload",
    "PLATFORM_METADATA":{
        "gcp_project":"test_project",
        "gcp_cluster_location":"test_location",
        "gcp_cluster_name":"test_cluster"
    },
    "ISTIO_VERSION":"istio-1.4",
    "MESH_ID":"test-mesh"
  }
  )EOF";

// google::protobuf::Struct a;
// google::protobuf::util::JsonStringToMessage(configuration+'hfdjfhkjhdskhjk', a);

const static char encodeLookup[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const static char padCharacter = '=';

std::string base64Encode(const uint8_t* start, const uint8_t* end) {
  std::string encodedString;
  size_t size = end - start;
  encodedString.reserve(((size / 3) + (size % 3 > 0)) * 4);
  uint32_t temp;
  auto cursor = start;
  for (size_t idx = 0; idx < size / 3; idx++) {
    temp = (*cursor++) << 16; // Convert to big endian
    temp += (*cursor++) << 8;
    temp += (*cursor++);
    encodedString.append(1, encodeLookup[(temp & 0x00FC0000) >> 18]);
    encodedString.append(1, encodeLookup[(temp & 0x0003F000) >> 12]);
    encodedString.append(1, encodeLookup[(temp & 0x00000FC0) >> 6]);
    encodedString.append(1, encodeLookup[(temp & 0x0000003F)]);
  }
  switch (size % 3) {
  case 1:
    temp = (*cursor++) << 16; // Convert to big endian
    encodedString.append(1, encodeLookup[(temp & 0x00FC0000) >> 18]);
    encodedString.append(1, encodeLookup[(temp & 0x0003F000) >> 12]);
    encodedString.append(2, padCharacter);
    break;
  case 2:
    temp = (*cursor++) << 16; // Convert to big endian
    temp += (*cursor++) << 8;
    encodedString.append(1, encodeLookup[(temp & 0x00FC0000) >> 18]);
    encodedString.append(1, encodeLookup[(temp & 0x0003F000) >> 12]);
    encodedString.append(1, encodeLookup[(temp & 0x00000FC0) >> 6]);
    encodedString.append(1, padCharacter);
    break;
  }
  return encodedString;
}

bool base64Decode(const std::basic_string<char>& input, std::vector<uint8_t>* output) {
  if (input.length() % 4)
    return false;
  size_t padding = 0;
  if (input.length()) {
    if (input[input.length() - 1] == padCharacter)
      padding++;
    if (input[input.length() - 2] == padCharacter)
      padding++;
  }
  // Setup a vector to hold the result
  std::vector<unsigned char> decodedBytes;
  decodedBytes.reserve(((input.length() / 4) * 3) - padding);
  uint32_t temp = 0; // Holds decoded quanta
  std::basic_string<char>::const_iterator cursor = input.begin();
  while (cursor < input.end()) {
    for (size_t quantumPosition = 0; quantumPosition < 4; quantumPosition++) {
      temp <<= 6;
      if (*cursor >= 0x41 && *cursor <= 0x5A) // This area will need tweaking if
        temp |= *cursor - 0x41;               // you are using an alternate alphabet
      else if (*cursor >= 0x61 && *cursor <= 0x7A)
        temp |= *cursor - 0x47;
      else if (*cursor >= 0x30 && *cursor <= 0x39)
        temp |= *cursor + 0x04;
      else if (*cursor == 0x2B)
        temp |= 0x3E; // change to 0x2D for URL alphabet
      else if (*cursor == 0x2F)
        temp |= 0x3F;                     // change to 0x5F for URL alphabet
      else if (*cursor == padCharacter) { // pad
        switch (input.end() - cursor) {
        case 1: // One pad character
          decodedBytes.push_back((temp >> 16) & 0x000000FF);
          decodedBytes.push_back((temp >> 8) & 0x000000FF);
          goto Ldone;
        case 2: // Two pad characters
          decodedBytes.push_back((temp >> 10) & 0x000000FF);
          goto Ldone;
        default:
          return false;
        }
      } else
        return false;
      cursor++;
    }
    decodedBytes.push_back((temp >> 16) & 0x000000FF);
    decodedBytes.push_back((temp >> 8) & 0x000000FF);
    decodedBytes.push_back((temp)&0x000000FF);
  }
Ldone:
  *output = std::move(decodedBytes);
  return true;
}
std::string check_compiler;

void (*test_fn)() = nullptr;

void empty_test() {}

void get_current_time_test() {
  uint64_t t;
  if (WasmResult::Ok != proxy_get_current_time_nanoseconds(&t)) {
    logError("bad result from getCurrentTimeNanoseconds");
  }
}

void small_string_check_compiler_test() {
  check_compiler = "foo";
  check_compiler += "bar";
  check_compiler = "";
}

void small_string_test() {
  std::string s = "foo";
  s += "bar";
  xDoNotRemove = s.size();
}

void small_string_check_compiler1000_test() {
  for (int x = 0; x < 1000; x++) {
    check_compiler = "foo";
    check_compiler += "bar";
  }
  check_compiler = "";
}

void small_string1000_test() {
  for (int x = 0; x < 1000; x++) {
    std::string s = "foo";
    s += "bar";
    xDoNotRemove += s.size();
  }
}

void large_string_test() {
  std::string s(1024, 'f');
  std::string d(1024, 'o');
  s += d;
  xDoNotRemove += s.size();
}

void large_string1000_test() {
  for (int x = 0; x < 1000; x++) {
    std::string s(1024, 'f');
    std::string d(1024, 'o');
    s += d;
    xDoNotRemove += s.size();
  }
}

void get_property_test() {
  std::string property = "plugin_root_id";
  const char* value_ptr = nullptr;
  size_t value_size = 0;
  auto result = proxy_get_property(property.data(), property.size(), &value_ptr, &value_size);
  if (WasmResult::Ok != result) {
    logError("bad result for getProperty");
  }
  ::free(reinterpret_cast<void*>(const_cast<char*>(value_ptr)));
}

void grpc_service_test() {
  std::string value = "foo";
  GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name(value);
  std::string grpc_service_string;
  grpc_service.SerializeToString(&grpc_service_string);
}

void grpc_service1000_test() {
  std::string value = "foo";
  for (int x = 0; x < 1000; x++) {
    GrpcService grpc_service;
    grpc_service.mutable_envoy_grpc()->set_cluster_name(value);
    std::string grpc_service_string;
    grpc_service.SerializeToString(&grpc_service_string);
  }
}

void modify_metadata_test() {
  auto path = getRequestHeader(":path");
  addRequestHeader("newheader", "newheadervalue");
  auto server = getRequestHeader("server");
  replaceRequestHeader("server", "envoy-wasm");
  replaceRequestHeader("envoy-wasm", "server");
  removeRequestHeader("newheader");
}

void modify_metadata1000_test() {
  for (int x = 0; x < 1000; x++) {
    auto path = getRequestHeader(":path");
    addRequestHeader("newheader", "newheadervalue");
    auto server = getRequestHeader("server");
    replaceRequestHeader("server", "envoy-wasm");
    replaceRequestHeader("envoy-wasm", "server");
    removeRequestHeader("newheader");
  }
}

void json_serialize_test() { google::protobuf::util::JsonStringToMessage(configuration, &args); }

void json_serialize_arena_test() {
  google::protobuf::util::JsonStringToMessage(configuration, args_arena);
}

void json_deserialize_test() {
  std::string json;
  google::protobuf::util::MessageToJsonString(args, &json);
  xDoNotRemove += json.size();
}

void json_deserialize_arena_test() {
  std::string json;
  google::protobuf::util::MessageToJsonString(*args_arena, &json);
}

void json_deserialize_empty_test() {
  std::string json;
  google::protobuf::Struct empty;
  google::protobuf::util::MessageToJsonString(empty, &json);
  xDoNotRemove = json.size();
}

void json_serialize_deserialize_test() {
  std::string json;
  google::protobuf::Struct proto;
  google::protobuf::util::JsonStringToMessage(configuration, &proto);
  google::protobuf::util::MessageToJsonString(proto, &json);
  xDoNotRemove = json.size();
}

void convert_to_filter_state_test() {
  auto start = reinterpret_cast<uint8_t*>(&*configuration.begin());
  auto end = start + configuration.size();
  std::string encoded_config = base64Encode(start, end);
  std::vector<uint8_t> decoded;
  base64Decode(encoded_config, &decoded);
  std::string decoded_config(decoded.begin(), decoded.end());
  google::protobuf::util::JsonStringToMessage(decoded_config, &args);
  auto bytes = args.SerializeAsString();
  setFilterStateStringValue("wasm_request_set_key", bytes);
}

WASM_EXPORT(uint32_t, proxy_on_vm_start, (uint32_t, uint32_t configuration_size)) {
  const char* configuration_ptr = nullptr;
  size_t size;
  proxy_get_buffer_bytes(WasmBufferType::VmConfiguration, 0, configuration_size, &configuration_ptr,
                         &size);
  std::string configuration(configuration_ptr, size);
  if (configuration == "empty") {
    test_fn = &empty_test;
  } else if (configuration == "get_current_time") {
    test_fn = &get_current_time_test;
  } else if (configuration == "small_string") {
    test_fn = &small_string_test;
  } else if (configuration == "small_string1000") {
    test_fn = &small_string1000_test;
  } else if (configuration == "small_string_check_compiler") {
    test_fn = &small_string_check_compiler_test;
  } else if (configuration == "small_string_check_compiler1000") {
    test_fn = &small_string_check_compiler1000_test;
  } else if (configuration == "large_string") {
    test_fn = &large_string_test;
  } else if (configuration == "large_string1000") {
    test_fn = &large_string1000_test;
  } else if (configuration == "get_property") {
    test_fn = &get_property_test;
  } else if (configuration == "grpc_service") {
    test_fn = &grpc_service_test;
  } else if (configuration == "grpc_service1000") {
    test_fn = &grpc_service1000_test;
  } else if (configuration == "modify_metadata") {
    test_fn = &modify_metadata_test;
  } else if (configuration == "modify_metadata1000") {
    test_fn = &modify_metadata1000_test;
  } else if (configuration == "json_serialize") {
    test_fn = &json_serialize_test;
  } else if (configuration == "json_serialize_arena") {
    test_fn = &json_serialize_arena_test;
  } else if (configuration == "json_deserialize") {
    test_fn = &json_deserialize_test;
  } else if (configuration == "json_deserialize_empty") {
    test_fn = &json_deserialize_empty_test;
  } else if (configuration == "json_deserialize_arena") {
    test_fn = &json_deserialize_arena_test;
  } else if (configuration == "json_serialize_deserialize") {
    test_fn = &json_serialize_deserialize_test;
  } else if (configuration == "convert_to_filter_state") {
    test_fn = &convert_to_filter_state_test;
  } else {
    std::string message = "on_start " + configuration;
    proxy_log(LogLevel::info, message.c_str(), message.size());
  }
  ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
  return 1;
}

WASM_EXPORT(void, proxy_on_tick, (uint32_t)) { (*test_fn)(); }

END_WASM_PLUGIN
