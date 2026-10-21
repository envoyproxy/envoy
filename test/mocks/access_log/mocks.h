#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace AccessLog {

class MockAccessLogFile : public AccessLogFile {
public:
  MockAccessLogFile();
  ~MockAccessLogFile() override;

  // AccessLog::AccessLogFile
  MOCK_METHOD(void, write, (absl::string_view data), (override));
  MOCK_METHOD(void, reopen, (), (override));
  MOCK_METHOD(void, flush, (), (override));
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter() override;

  // AccessLog::Filter
  MOCK_METHOD(bool, evaluate, (const Formatter::Context&, const StreamInfo::StreamInfo&),
              (const, override));
};

class MockAccessLogManager : public AccessLogManager {
public:
  MockAccessLogManager();
  ~MockAccessLogManager() override;

  // AccessLog::AccessLogManager
  MOCK_METHOD(void, reopen, (), (override));
  MOCK_METHOD(absl::StatusOr<AccessLogFileSharedPtr>, createAccessLog,
              (const Envoy::Filesystem::FilePathAndType& file_info), (override));

  std::shared_ptr<MockAccessLogFile> file_{new testing::NiceMock<MockAccessLogFile>()};
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // AccessLog::Instance
  MOCK_METHOD(void, log, (const Formatter::Context&, const StreamInfo::StreamInfo&), (override));
};

} // namespace AccessLog
} // namespace Envoy
