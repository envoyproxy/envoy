#include "common/network/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    try {
      Network::Utility::parseInternetAddressAndPort(string_buffer);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
      return;
    }
  }

  {
    const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
    std::list<Network::PortRange> port_range_list;
    try {
      Network::Utility::parsePortRangeList(string_buffer, port_range_list);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
      return;
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
