// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstdlib>

#include "test/test_common/file_system_for_test.h"

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "fmt/printf.h"
#include "gtest/gtest.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche_platform_impl/quiche_test_output_impl.h"

namespace quiche {
namespace {

void quicheRecordTestOutputToFile(const std::string& filename, absl::string_view data) {
  const char* output_dir_env = std::getenv("QUICHE_TEST_OUTPUT_DIR");
  if (output_dir_env == nullptr) {
    QUICHE_LOG(WARNING) << "Could not save test output since QUICHE_TEST_OUTPUT_DIR is not set";
    return;
  }

  std::string output_dir = output_dir_env;
  if (output_dir.empty()) {
    QUICHE_LOG(WARNING) << "Could not save test output since QUICHE_TEST_OUTPUT_DIR is empty";
    return;
  }

  if (output_dir.back() != '/') {
    output_dir += '/';
  }

  Envoy::Filesystem::Instance& file_system = Envoy::Filesystem::fileSystemForTest();
  if (!file_system.directoryExists(output_dir)) {
    QUICHE_LOG(ERROR) << "Directory does not exist while writing test output: " << output_dir;
    return;
  }

  static constexpr Envoy::Filesystem::FlagSet DefaultFlags{
      1 << Envoy::Filesystem::File::Operation::Read |
      1 << Envoy::Filesystem::File::Operation::Write |
      1 << Envoy::Filesystem::File::Operation::Create};

  const std::string output_path = output_dir + filename;
  Envoy::Filesystem::FilePathAndType new_file_info{Envoy::Filesystem::DestinationType::File,
                                                   output_path};
  Envoy::Filesystem::FilePtr file = file_system.createFile(new_file_info);
  if (!file->open(DefaultFlags).return_value_) {
    QUICHE_LOG(ERROR) << "Failed to open test output file: " << output_path;
    return;
  }

  if (file->write(data).return_value_ != static_cast<ssize_t>(data.size())) {
    QUICHE_LOG(ERROR) << "Failed to write to test output file: " << output_path;
  } else {
    QUICHE_LOG(INFO) << "Recorded test output into " << output_path;
  }

  file->close();
}
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
void QuicheSaveTestOutputImpl(absl::string_view filename, absl::string_view data) {
  quicheRecordTestOutputToFile(filename.data(), data);
}

// NOLINTNEXTLINE(readability-identifier-naming)
bool QuicheLoadTestOutputImpl(absl::string_view filename, std::string* data) {
  const char* read_dir_env = std::getenv("QUICHE_TEST_OUTPUT_DIR");
  if (read_dir_env == nullptr) {
    QUICHE_LOG(WARNING) << "Could not load test output since QUICHE_TEST_OUTPUT_DIR is not set";
    return false;
  }

  std::string read_dir = read_dir_env;
  if (read_dir.empty()) {
    QUICHE_LOG(WARNING) << "Could not load test output since QUICHE_TEST_OUTPUT_DIR is empty";
    return false;
  }

  if (read_dir.back() != '/') {
    read_dir += '/';
  }

  const std::string read_path = read_dir + filename.data();

  Envoy::Filesystem::Instance& file_system = Envoy::Filesystem::fileSystemForTest();
  if (!file_system.fileExists(read_path)) {
    QUICHE_LOG(ERROR) << "Test output file does not exist: " << read_path;
    return false;
  }
  *data = file_system.fileReadToEnd(read_path).value();
  return true;
}

// NOLINTNEXTLINE(readability-identifier-naming)
void QuicheRecordTraceImpl(absl::string_view identifier, absl::string_view data) {
  const testing::TestInfo* test_info = testing::UnitTest::GetInstance()->current_test_info();

  std::string timestamp = absl::FormatTime("%Y%m%d%H%M%S", absl::Now(), absl::LocalTimeZone());

  std::string filename = fmt::sprintf("%s.%s.%s.%s.qtr", test_info->name(),
                                      test_info->test_case_name(), identifier.data(), timestamp);

  quicheRecordTestOutputToFile(filename, data);
}

} // namespace quiche
