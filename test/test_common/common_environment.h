#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/server/options.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace Envoy {

class CommonTestEnvironment {
public:
  using PortMap = absl::node_hash_map<std::string, uint32_t>;

  using ParamMap = absl::node_hash_map<std::string, std::string>;

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
   * Return a vector of spdlog loggers as parameters to test. Tests are mainly
   * for the behavior consistency between default loggers and fine-grained loggers.
   * @return std::vector<spdlog::logger*>
   */
  static std::vector<spdlog::logger*> getSpdLoggersForTest();

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

#ifndef TARGET_OS_IOS
  /**
   * Execute a program under ::system. Any failure is fatal.
   * @param args program path and arguments.
   */
  static void exec(const std::vector<std::string>& args);
#endif

  /**
   * Dumps the contents of the string into a temporary file from temporaryDirectory() + filename.
   *
   * @param filename: the name of the file to use
   * @param contents: the data to go in the file.
   * @param fully_qualified_path: if true, will write to filename without prepending the tempdir.
   * @param unlink: if true will delete any prior file before writing.
   * @return the fully qualified path of the output file.
   */
  static std::string writeStringToFileForTest(const std::string& filename,
                                              const std::string& contents,
                                              bool fully_qualified_path = false,
                                              bool unlink = true);
  /**
   * Dumps the contents of the file into the string.
   *
   * @param filename: the fully qualified name of the file to use
   */
  static std::string readFileToStringForTest(const std::string& filename);

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
