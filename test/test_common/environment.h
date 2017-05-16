#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/server/options.h"

#include "common/json/json_loader.h"

namespace Envoy {
class TestEnvironment {
public:
  typedef std::unordered_map<std::string, uint32_t> PortMap;

  /**
   * Initialize command-line options for later access by tests in getOptions().
   * @param argc number of command-line args.
   * @param argv array of command-line args.
   */
  static void initializeOptions(int argc, char** argv);

  /**
   * Check whether testing with IP version type {v4 or v6} is enabled via
   * setting the environment variable ENVOY_IP_TEST_VERSIONS.
   * @param Network::Address::IpVersion IP address version to check.
   * @return bool if testing only with IP type addresses only.
   */
  static bool shouldRunTestForIpVersion(const Network::Address::IpVersion& type);

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
   * Obtain command-line options reference.
   * @return Server::Options& with command-line options.
   */
  static Server::Options& getOptions();

  /**
   * Obtain the value of an environment variable, die if not available.
   * @return std::string with the value of the environment variable.
   */
  static std::string getCheckedEnvVar(const std::string& var);

  /**
   * Obtain a private writable temporary directory.
   * @return const std::string& with the path to the temporary directory.
   */
  static const std::string& temporaryDirectory();

  /**
   * Prefix a given path with the private writable test temporary directory.
   * @param path path suffix.
   * @return std::string path qualified with temporary directory.
   */
  static std::string temporaryPath(const std::string& path) {
    return temporaryDirectory() + "/" + path;
  }

  /**
   * Obtain read-only test input data directory.
   * @return const std::string& with the path to the read-only test input directory.
   */
  static const std::string& runfilesDirectory();

  /**
   * Prefix a given path with the read-only test input data directory.
   * @param path path suffix.
   * @return std::string path qualified with read-only test input data directory.
   */
  static std::string runfilesPath(const std::string& path) {
    return runfilesDirectory() + "/" + path;
  }

  /**
   * Obtain Unix Domain Socket temporary directory.
   * @return std::string& with the path to the Unix Domain Socket temporary directory.
   */
  static const std::string unixDomainSocketDirectory();

  /**
   * Prefix a given path with the Unix Domain Socket temporary directory.
   * @param path path suffix.
   * @return std::string path qualified with the Unix Domain Socket temporary directory.
   */
  static std::string unixDomainSocketPath(const std::string& path) {
    return unixDomainSocketDirectory() + "/" + path;
  }

  /**
   * String environment path substitution.
   * @param str string with template patterns including {{ test_tmpdir }}.
   * @return std::string with patterns replaced with environment values.
   */
  static std::string substitute(const std::string str);

  // TODO(hennna): Deprecate after IPv6 test support is finished.
  /**
   * Substitue ports and paths in a JSON file in the private writable test temporary directory.
   * @param path path prefix for the input file with port and path templates.
   * @param port_map map from port name to port number.
   * @return std::string path for the generated file.
   */
  static std::string temporaryFileSubstitute(const std::string& path, const PortMap& port_map);

  /**
   * Substitue ports, paths, and IP loopback addressses in a JSON file in the
   * private writable test temporary directory.
   * @param path path prefix for the input file with port and path templates.
   * @param version IP address version to substitute.
   * @param port_map map from port name to port number.
   * @return std::string path for the generated file.
   */
  static std::string temporaryFileSubstitute(const std::string& path,
                                             const Network::Address::IpVersion& version,
                                             const PortMap& port_map);

  /**
   * Build JSON object from a string subject to environment path substitution.
   * @param json JSON with template patterns including {{ test_certs }}.
   * @return Json::ObjectSharedPtr with built JSON object.
   */
  static Json::ObjectSharedPtr jsonLoadFromString(const std::string& json);

  /**
   * Execute a program under ::system. Any failure is fatal.
   * @param args program path and arguments.
   */
  static void exec(const std::vector<std::string>& args);
};
} // Envoy
