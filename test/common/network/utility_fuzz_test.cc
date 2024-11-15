#include "envoy/config/core/v3/address.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string string_buffer(reinterpret_cast<const char*>(buf), len);

  try {
    Network::Utility::parseInternetAddressNoThrow(string_buffer);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }

  Network::Utility::parseInternetAddressAndPortNoThrow(string_buffer);

  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe()->set_path(string_buffer);
    Network::Utility::protobufAddressToAddressNoThrow(proto_address);
  }

  {
    FuzzedDataProvider provider(buf, len);
    envoy::config::core::v3::Address proto_address;
    const auto port_value = provider.ConsumeIntegral<uint16_t>();
    const std::string address_value = provider.ConsumeRemainingBytesAsString();
    proto_address.mutable_socket_address()->set_address(address_value);
    proto_address.mutable_socket_address()->set_port_value(port_value);
    Network::Utility::protobufAddressToAddressNoThrow(proto_address);
  }

  try {
    envoy::config::core::v3::Address proto_address;
    Network::Address::Ipv4Instance address(string_buffer);
    Network::Utility::addressToProtobufAddress(address, proto_address);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }

  envoy::config::core::v3::Address proto_address;
  auto address_or_error = Network::Address::PipeInstance::create(string_buffer);
  if (address_or_error.status().ok()) {
    Network::Utility::addressToProtobufAddress(*(address_or_error.value()), proto_address);
  }
}

} // namespace Fuzz
} // namespace Envoy
