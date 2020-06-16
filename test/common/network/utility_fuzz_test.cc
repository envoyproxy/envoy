#include "envoy/config/core/v3/address.pb.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    try {
      Network::Utility::parseInternetAddress(string_buffer);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    try {
      Network::Utility::parseInternetAddressAndPort(string_buffer);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    std::list<Network::PortRange> port_range_list;
    try {
      Network::Utility::parsePortRangeList(string_buffer, port_range_list);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    envoy::config::core::v3::Address proto_address;
    try {
      proto_address.mutable_pipe()->set_path(string_buffer);
      Network::Utility::protobufAddressToAddress(proto_address);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    FuzzedDataProvider provider(buf, len);
    envoy::config::core::v3::Address proto_address;
    const auto port_value = provider.ConsumeIntegral<uint16_t>();
    const std::string address_value = provider.ConsumeRemainingBytesAsString();
    try {
      proto_address.mutable_socket_address()->set_address(address_value);
      proto_address.mutable_socket_address()->set_port_value(port_value);
      Network::Utility::protobufAddressToAddress(proto_address);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    envoy::config::core::v3::Address proto_address;
    try {
      Network::Address::Ipv4Instance address(string_buffer);
      Network::Utility::addressToProtobufAddress(address, proto_address);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    envoy::config::core::v3::Address proto_address;
    try {
      Network::Address::PipeInstance address(string_buffer);
      Network::Utility::addressToProtobufAddress(address, proto_address);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
