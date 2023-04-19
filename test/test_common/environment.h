#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/server/options.h"

#include "source/common/json/json_loader.h"

#include "test/test_common/common_environment.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

namespace Grpc {

// Support parameterizing over gRPC client type.
enum class ClientType { EnvoyGrpc, GoogleGrpc };

} // namespace Grpc

class TestEnvironment : public CommonTestEnvironment {
public:
  using PortMap = absl::node_hash_map<std::string, uint32_t>;

  using ParamMap = absl::node_hash_map<std::string, std::string>;

  /**
   * Check whether testing with IP version type {v4 or v6} is enabled via
   * setting the environment variable ENVOY_IP_TEST_VERSIONS.
   * @param Network::Address::IpVersion IP address version to check.
   * @return bool if testing only with IP type addresses only.
   */
  static bool shouldRunTestForIpVersion(Network::Address::IpVersion type);

  /**
   * Return a vector of IP address parameters to test. Tests can be run with
   * only IPv4 addressing or only IPv6 addressing by setting the environment
   * variable ENVOY_IP_TEST_VERSIONS to "v4only" or "v6only", respectively.
   * The default test setting runs all tests with both IPv4 and IPv6 addresses.
   * @return std::vector<Network::Address::IpVersion> vector of IP address
   * types to test.
   */
  static std::vector<Network::Address::IpVersion> getIpVersionsForTest();

  /**
   * Tests can be run with Envoy Grpc and Google Grpc or Envoy Grpc alone by setting compiler option
   * `--define google_grpc=disabled`.
   * @return a vector of Grpc versions to test.
   */
  static std::vector<Grpc::ClientType> getsGrpcVersionsForTest();

  /**
   * Obtain platform specific new line character(s)
   * @return absl::string_view platform specific new line character(s)
   */
  static constexpr absl::string_view newLine
#ifdef WIN32
      {"\r\n"};
#else
      {"\n"};
#endif

  /**
   * String environment path, loopback, and DNS resolver type substitution.
   * @param str string with template patterns including {{ test_tmpdir }}.
   * @param version supplies the IP version to substitute for relevant templates.
   * @return std::string with patterns replaced with environment values.
   */
  static std::string
  substitute(const std::string& str,
             Network::Address::IpVersion version = Network::Address::IpVersion::v4);

  /**
   * Substitute ports, paths, and IP loopback addresses in a JSON file in the
   * private writable test temporary directory.
   * @param path path prefix for the input file with port and path templates.
   * @param port_map map from port name to port number.
   * @param version IP address version to substitute.
   * @return std::string path for the generated file.
   */
  static std::string temporaryFileSubstitute(const std::string& path, const PortMap& port_map,
                                             Network::Address::IpVersion version);
  /**
   * Substitute ports, paths, and IP loopback addresses in a JSON file in the
   * private writable test temporary directory.
   * @param path path prefix for the input file with port and path templates.
   * @param param_map map from parameter name to values.
   * @param port_map map from port name to port number.
   * @param version IP address version to substitute.
   * @return std::string path for the generated file.
   */
  static std::string temporaryFileSubstitute(const std::string& path, const ParamMap& param_map,
                                             const PortMap& port_map,
                                             Network::Address::IpVersion version);

  /**
   * Build JSON object from a string subject to environment path, loopback, and DNS resolver type
   * substitution.
   * @param json JSON with template patterns including {{ test_certs }}.
   * @param version supplies the IP version to substitute for relevant templates.
   * @return Json::ObjectSharedPtr with built JSON object.
   */
  static Json::ObjectSharedPtr
  jsonLoadFromString(const std::string& json,
                     Network::Address::IpVersion version = Network::Address::IpVersion::v4);

};

} // namespace Envoy
