#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace AccessLog {

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
  MOCK_METHOD3(log, void(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers, const RequestInfo& request_info));
};

class MockRequestInfo : public RequestInfo {
public:
  MockRequestInfo();
  ~MockRequestInfo();

  // AccessLog::RequestInfo
  MOCK_METHOD1(setResponseFlag, void(ResponseFlag response_flag));
  MOCK_METHOD1(onUpstreamHostSelected, void(Upstream::HostDescriptionConstSharedPtr host));
  MOCK_CONST_METHOD0(startTime, SystemTime());
  MOCK_CONST_METHOD0(requestReceivedDuration, const Optional<std::chrono::microseconds>&());
  MOCK_METHOD1(requestReceivedDuration, void(MonotonicTime time));
  MOCK_CONST_METHOD0(responseReceivedDuration, const Optional<std::chrono::microseconds>&());
  MOCK_METHOD1(responseReceivedDuration, void(MonotonicTime time));
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_CONST_METHOD0(protocol, const Optional<Http::Protocol>&());
  MOCK_METHOD1(protocol, void(Http::Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, Optional<uint32_t>&());
  MOCK_CONST_METHOD0(bytesSent, uint64_t());
  MOCK_CONST_METHOD0(duration, std::chrono::microseconds());
  MOCK_CONST_METHOD1(getResponseFlag, bool(AccessLog::ResponseFlag));
  MOCK_CONST_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_CONST_METHOD0(upstreamLocalAddress, const Optional<std::string>&());
  MOCK_CONST_METHOD0(healthCheck, bool());
  MOCK_METHOD1(healthCheck, void(bool is_hc));
  MOCK_CONST_METHOD0(getDownstreamAddress, const std::string&());

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
  SystemTime start_time_;
  Optional<std::chrono::microseconds> request_received_duration_;
  Optional<std::chrono::microseconds> response_received_duration_;
};

} // namespace AccessLog
} // namespace Envoy
