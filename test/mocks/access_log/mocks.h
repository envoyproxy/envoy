#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "test/mocks/filesystem/mocks.h"

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

} // namespace AccessLog
} // namespace Envoy
