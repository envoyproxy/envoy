#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "test/mocks/filesystem/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace AccessLog {

class MockFilter : public Filter {
public:
  MockFilter();
  ~MockFilter();

  // AccessLog::Filter
  MOCK_METHOD2(evaluate,
               bool(const RequestInfo::RequestInfo& info, const Http::HeaderMap& request_headers));
};

class MockAccessLogManager : public AccessLogManager {
public:
  MockAccessLogManager();
  ~MockAccessLogManager();

  // AccessLog::AccessLogManager
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD1(createAccessLog, Filesystem::FileSharedPtr(const std::string& file_name));

  std::shared_ptr<Filesystem::MockFile> file_{new testing::NiceMock<Filesystem::MockFile>()};
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // AccessLog::Instance
  MOCK_METHOD4(log,
               void(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                    const Http::HeaderMap* response_trailers,
                    const RequestInfo::RequestInfo& request_info));
};

} // namespace AccessLog
} // namespace Envoy
