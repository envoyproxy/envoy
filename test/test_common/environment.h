#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/server/options.h"

#include "common/json/json_loader.h"

#include "absl/types/optional.h"

namespace Envoy {
class TestEnvironment {
public:
  typedef std::unordered_map<std::string, uint32_t> PortMap;

  typedef std::unordered_map<std::string, std::string> ParamMap;

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
   * Obtain command-line options reference.
   * @return Server::Options& with command-line options.
   */
  static Server::Options& getOptions();

  /**
   * Obtain the value of an environment variable, null if not available.
   * @return absl::optional<std::string> with the value of the environment variable.
   */
  static absl::optional<std::string> getOptionalEnvVar(const std::string& var);

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
   * @param abstract_namespace true if an abstract namespace should be returned.
   * @return std::string path qualified with the Unix Domain Socket temporary directory.
   */
  static std::string unixDomainSocketPath(const std::string& path,
                                          bool abstract_namespace = false) {
    return (abstract_namespace ? "@" : "") + unixDomainSocketDirectory() + "/" + path;
  }

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
   * Substitute ports, paths, and IP loopback addressses in a JSON file in the
   * private writable test temporary directory.
   * @param path path prefix for the input file with port and path templates.
   * @param port_map map from port name to port number.
   * @param version IP address version to substitute.
   * @return std::string path for the generated file.
   */
  static std::string temporaryFileSubstitute(const std::string& path, const PortMap& port_map,
                                             Network::Address::IpVersion version);
  /**
   * Substitute ports, paths, and IP loopback addressses in a JSON file in the
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

  /**
   * Execute a program under ::system. Any failure is fatal.
   * @param args program path and arguments.
   */
  static void exec(const std::vector<std::string>& args);

  /**
   * Dumps the contents of the string into a temporary file from temporaryDirectory() + filename.
   *
   * @param filename: the name of the file to use
   * @param contents: the data to go in the file.
   * @return the fully qualified path of the output file.
   */
  static std::string writeStringToFileForTest(const std::string& filename,
                                              const std::string& contents);
  /**
   * Dumps the contents of the file into the string.
   *
   * @param filename: the fully qualified name of the file to use
   * @return string the contents of the file.
   */
  static std::string readFileToStringForTest(const std::string& filename);

  /**
   * Create a path on the filesystem (mkdir -p ... equivalent).
   * @param path.
   */
  static void createPath(const std::string& path);

  /**
   * Create a parent path on the filesystem (mkdir -p $(dirname ...) equivalent).
   * @param path.
   */
  static void createParentPath(const std::string& path);

  /**
   * Remove a path on the filesystem (rm -rf ... equivalent).
   * @param path.
   */
  static void removePath(const std::string& path);
};
} // namespace Envoy
