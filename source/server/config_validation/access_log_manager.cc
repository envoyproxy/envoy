#include "server/config_validation/access_log_manager.h"

namespace Envoy {
namespace AccessLog {
namespace {

class NullAccessLogFile : public AccessLogFile {
public:
  explicit NullAccessLogFile(std::string file_name) : file_name_(std::move(file_name)) {}

  // AccessLogFile impl
  void write(absl::string_view) override {
    RELEASE_ASSERT(false, absl::StrCat("Can't write data to a null access log for ", file_name_));
  }
  void reopen() override {}
  void flush() override {}

private:
  const std::string file_name_;
};

} // namespace

AccessLogFileSharedPtr NullAccessLogManager::createAccessLog(const std::string& file_name) {
  return std::make_shared<NullAccessLogFile>(file_name);
}

} // namespace AccessLog
} // namespace Envoy