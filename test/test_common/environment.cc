#include "test/test_common/environment.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/compiler_requirements.h"
#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/directory.h"

#include "absl/container/node_hash_map.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#endif

#include "source/server/options_impl.h"

#include "test/test_common/common_environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/network_utility.h"

#include "absl/debugging/symbolize.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"


namespace Envoy {

bool TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion type) {
  const char* value = std::getenv("ENVOY_IP_TEST_VERSIONS");
  std::string option(value ? value : "");
  if (option.empty()) {
    return true;
  }
  if ((type == Network::Address::IpVersion::v4 && option == "v6only") ||
      (type == Network::Address::IpVersion::v6 && option == "v4only")) {
    return false;
  }
  return true;
}

std::vector<Network::Address::IpVersion> TestEnvironment::getIpVersionsForTest() {
  std::vector<Network::Address::IpVersion> parameters;
  for (auto version : {Network::Address::IpVersion::v4, Network::Address::IpVersion::v6}) {
    if (TestEnvironment::shouldRunTestForIpVersion(version)) {
      parameters.push_back(version);
      if (!Network::Test::supportsIpVersion(version)) {
        const auto version_string = Network::Test::addressVersionAsString(version);
        ENVOY_LOG_TO_LOGGER(
            Logger::Registry::getLog(Logger::Id::testing), warn,
            "Testing with IP{} addresses may not be supported on this machine. If "
            "testing fails, set the environment variable ENVOY_IP_TEST_VERSIONS to 'v{}only'.",
            version_string, version_string);
      }
    }
  }
  return parameters;
}

std::vector<Grpc::ClientType> TestEnvironment::getsGrpcVersionsForTest() {
#ifdef ENVOY_GOOGLE_GRPC
  return {Grpc::ClientType::EnvoyGrpc, Grpc::ClientType::GoogleGrpc};
#else
  return {Grpc::ClientType::EnvoyGrpc};
#endif
}

std::string TestEnvironment::substitute(const std::string& str,
                                        Network::Address::IpVersion version) {
  const absl::node_hash_map<std::string, std::string> path_map = {
      {"test_tmpdir", CommonTestEnvironment::temporaryDirectory()},
      {"test_udsdir", CommonTestEnvironment::unixDomainSocketDirectory()},
      {"test_rundir", CommonTestEnvironment::runfilesDirectory()},
  };

  std::string out_json_string = str;
  for (const auto& it : path_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, it.second);
  }

  // Substitute platform specific null device.
  const std::regex null_device_regex(R"(\{\{ null_device_path \}\})");
  out_json_string = std::regex_replace(out_json_string, null_device_regex,
                                       std::string(Platform::null_device_path).c_str());

  // Substitute IP loopback addresses.
  const std::regex loopback_address_regex(R"(\{\{ ip_loopback_address \}\})");
  out_json_string = std::regex_replace(out_json_string, loopback_address_regex,
                                       Network::Test::getLoopbackAddressString(version));
  const std::regex ntop_loopback_address_regex(R"(\{\{ ntop_ip_loopback_address \}\})");
  out_json_string = std::regex_replace(out_json_string, ntop_loopback_address_regex,
                                       Network::Test::getLoopbackAddressString(version));

  // Substitute IP any addresses.
  const std::regex any_address_regex(R"(\{\{ ip_any_address \}\})");
  out_json_string = std::regex_replace(out_json_string, any_address_regex,
                                       Network::Test::getAnyAddressString(version));

  // Substitute dns lookup family.
  const std::regex lookup_family_regex(R"(\{\{ dns_lookup_family \}\})");
  switch (version) {
  case Network::Address::IpVersion::v4:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v4_only");
    break;
  case Network::Address::IpVersion::v6:
    out_json_string = std::regex_replace(out_json_string, lookup_family_regex, "v6_only");
    break;
  }

  // Substitute socket options arguments.
  const std::regex sol_socket_regex(R"(\{\{ sol_socket \}\})");
  out_json_string =
      std::regex_replace(out_json_string, sol_socket_regex, std::to_string(SOL_SOCKET));
  const std::regex so_reuseport_regex(R"(\{\{ so_reuseport \}\})");
  out_json_string =
      std::regex_replace(out_json_string, so_reuseport_regex, std::to_string(SO_REUSEPORT));

  return out_json_string;
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const PortMap& port_map,
                                                     Network::Address::IpVersion version) {
  return temporaryFileSubstitute(path, ParamMap(), port_map, version);
}

std::string TestEnvironment::temporaryFileSubstitute(const std::string& path,
                                                     const ParamMap& param_map,
                                                     const PortMap& port_map,
                                                     Network::Address::IpVersion version) {
  RELEASE_ASSERT(!path.empty(), "requested path to substitute in is empty");
  // Load the entire file as a string, regex replace one at a time and write it back out. Proper
  // templating might be better one day, but this works for now.
  const std::string json_path = CommonTestEnvironment::runfilesPath(path);
  std::string out_json_string = CommonTestEnvironment::readFileToStringForTest(json_path);

  // Substitute params.
  for (const auto& it : param_map) {
    const std::regex param_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, param_regex, it.second);
  }

  // Substitute ports.
  for (const auto& it : port_map) {
    const std::regex port_regex("\\{\\{ " + it.first + " \\}\\}");
    out_json_string = std::regex_replace(out_json_string, port_regex, std::to_string(it.second));
  }

  // Substitute paths and other common things.
  out_json_string = substitute(out_json_string, version);

  auto name = Filesystem::fileSystemForTest().splitPathFromFilename(path).file_;
  const std::string extension = std::string(name.substr(name.rfind('.')));
  const std::string out_json_path =
      CommonTestEnvironment::temporaryPath(name) + ".with.ports" + extension;
  {
    std::ofstream out_json_file(out_json_path, std::ios::binary);
    out_json_file << out_json_string;
  }
  return out_json_path;
}

Json::ObjectSharedPtr TestEnvironment::jsonLoadFromString(const std::string& json,
                                                          Network::Address::IpVersion version) {
  return Json::Factory::loadFromString(substitute(json, version));
}

} // namespace Envoy
