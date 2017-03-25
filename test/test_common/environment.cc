#include "test/test_common/environment.h"

#include "common/common/assert.h"

namespace {

std::string* getCheckedEnvVar(const std::string& var) {
  // Bazel style temp dirs. Should be set by test runner or Bazel.
  const char* path = ::getenv(var.c_str());
  RELEASE_ASSERT(path != nullptr);
  return new std::string(path);
}

} // namespace

const std::string& TestEnvironment::temporaryDirectory() {
  static const std::string* temporary_directory = getCheckedEnvVar("TEST_TMPDIR");
  return *temporary_directory;
}

const std::string& TestEnvironment::runfilesDirectory() {
  static const std::string* runfiles_directory = getCheckedEnvVar("TEST_SRCDIR");
  return *runfiles_directory;
}

std::string TestEnvironment::substitute(const std::string str) {
  // TODO(htuch): Add support for {{ test_tmpdir }} etc. as needed for tests.
  const std::regex test_cert_regex("\\{\\{ test_certs \\}\\}");
  return std::regex_replace(str, test_cert_regex, TestEnvironment::runfilesPath("test/certs"));
}

Json::ObjectPtr TestEnvironment::jsonLoadFromString(const std::string& json) {
  return Json::Factory::LoadFromString(substitute(json));
}
