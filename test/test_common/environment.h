#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/network/address.h"
#include "envoy/server/options.h"

#include "common/json/json_loader.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {
class TestEnvironment {
public:
  using PortMap = std::unordered_map<std::string, uint32_t>;

  using ParamMap = std::unordered_map<std::string, std::string>;

  /**
   * Perform common initialization steps needed to run a test binary. This
   * method should be called first in all test main functions.
   * @param program_name argv[0] test program is invoked with
   */
  static void initializeTestMain(char* program_name);

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
  static std::string temporaryPath(absl::string_view path) {
    return absl::StrCat(temporaryDirectory(), "/", path);
  }

  /**
   * Obtain platform specific null device path
   * @return const std::string& null device path
   */
  static const std::string& nullDevicePath();

  /**
   * Obtain read-only test input data directory.
   * @param workspace the name of the Bazel workspace where the input data is.
   * @return const std::string& with the path to the read-only test input directory.
   */
  static std::string runfilesDirectory(const std::string& workspace = "envoy");

  /**
   * Prefix a given path with the read-only test input data directory.
   * @param path path suffix.
   * @return std::string path qualified with read-only test input data directory.
   */
  static std::string runfilesPath(const std::string& path, const std::string& workspace = "envoy");

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
   * @param fully_qualified_path: if true, will write to filename without prepending the tempdir.
   * @return the fully qualified path of the output file.
   */
  static std::string writeStringToFileForTest(const std::string& filename,
                                              const std::string& contents,
                                              bool fully_qualified_path = false);
  /**
   * Dumps the contents of the file into the string.
   *
   * @param filename: the fully qualified name of the file to use
   * @param require_existence if true, RELEASE_ASSERT if the file does not exist.
   *   If false, an empty string will be returned if the file is not present.
   * @return string the contents of the file.
   */
  static std::string readFileToStringForTest(const std::string& filename,
                                             bool require_existence = true);

  /**
   * Create a path on the filesystem (mkdir -p ... equivalent).
   * @param path.
   */
  static void createPath(const std::string& path);

  /**
   * Remove a path on the filesystem (rm -rf ... equivalent).
   * @param path.
   */
  static void removePath(const std::string& path);

  /**
   * Rename a file
   * @param old_name
   * @param new_name
   */
  static void renameFile(const std::string& old_name, const std::string& new_name);

  /**
   * Create a symlink
   * @param target
   * @param link
   */
  static void createSymlink(const std::string& target, const std::string& link);

  /**
   * Set environment variable. Same args as setenv(2).
   */
  static void setEnvVar(const std::string& name, const std::string& value, int overwrite);

  /**
   * Removes environment variable. Same args as unsetenv(3).
   */
  static void unsetEnvVar(const std::string& name);

  /**
   * Set runfiles with current test, this have to be called before calling path related functions.
   */
  static void setRunfiles(bazel::tools::cpp::runfiles::Runfiles* runfiles);

private:
  static bazel::tools::cpp::runfiles::Runfiles* runfiles_;
};

/**
 * A utility class for atomically updating a file using symbolic link swap.
 * Note the file lifetime is limited to the instance of the AtomicFileUpdater
 * which erases any existing files upon creation, used for specific test
 * scenarios. See discussion at https://github.com/envoyproxy/envoy/pull/4298
 */
class AtomicFileUpdater {
public:
  AtomicFileUpdater(const std::string& filename);

  void update(const std::string& contents);

private:
  const std::string link_;
  const std::string new_link_;
  const std::string target1_;
  const std::string target2_;
  bool use_target1_;
};

} // namespace Envoy
