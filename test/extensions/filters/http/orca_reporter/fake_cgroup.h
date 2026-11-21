#pragma once

#include <cstdint>
#include <string>

#include "source/common/common/fmt.h"

#include "test/test_common/environment.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

// A directory with the cgroup v2 interface files the filter reads.
class FakeCgroup {
public:
  explicit FakeCgroup(absl::string_view name)
      : directory_(TestEnvironment::temporaryPath(absl::StrCat("orca_reporter/", name))) {
    TestEnvironment::removePath(directory_);
    TestEnvironment::createPath(directory_);
    setCpuUsage(0);
    setCpuMax("max 100000");
    setMemory(0, 0);
    setMemoryMax("max");
  }
  ~FakeCgroup() { TestEnvironment::removePath(directory_); }

  const std::string& directory() const { return directory_; }

  void setCpuUsage(uint64_t usage_usec) {
    // usage_usec is deliberately not the first key.
    write("cpu.stat",
          fmt::format("user_usec {}\nusage_usec {}\nsystem_usec 0\n", usage_usec, usage_usec));
  }
  void setCpuMax(absl::string_view value) { write("cpu.max", absl::StrCat(value, "\n")); }
  void setMemory(uint64_t current, uint64_t inactive_file) {
    write("memory.current", absl::StrCat(current, "\n"));
    write("memory.stat",
          fmt::format("anon 1\nactive_file 2\ninactive_file {}\nshmem 3\n", inactive_file));
  }
  void setMemoryMax(absl::string_view value) { write("memory.max", absl::StrCat(value, "\n")); }
  void removeFile(absl::string_view file) {
    TestEnvironment::removePath(absl::StrCat(directory_, "/", file));
  }

private:
  void write(absl::string_view file, const std::string& content) {
    TestEnvironment::writeStringToFileForTest(absl::StrCat(directory_, "/", file), content, true);
  }

  const std::string directory_;
};

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
