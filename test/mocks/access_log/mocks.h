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
  MOCK_METHOD1(write, void(absl::string_view data));
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD0(flush, void());
};

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter() override;

  // AccessLog::Filter
  MOCK_METHOD4(evaluate,
               bool(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                    const Http::HeaderMap& response_headers,
                    const Http::HeaderMap& response_trailers));
};

class MockAccessLogManager : public AccessLogManager {
public:
  MockAccessLogManager();
  ~MockAccessLogManager() override;

  // AccessLog::AccessLogManager
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD1(createAccessLog, AccessLogFileSharedPtr(const std::string& file_name));

  std::shared_ptr<MockAccessLogFile> file_{new testing::NiceMock<MockAccessLogFile>()};
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // AccessLog::Instance
  MOCK_METHOD4(log,
               void(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                    const Http::HeaderMap* response_trailers,
                    const StreamInfo::StreamInfo& stream_info));
};

} // namespace AccessLog
} // namespace Envoy
