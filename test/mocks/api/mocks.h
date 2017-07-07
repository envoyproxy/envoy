#pragma once

#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"

#include "test/mocks/filesystem/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Api {

class MockApi : public Api {
public:
  MockApi();
  ~MockApi();

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override {
    return Event::DispatcherPtr{allocateDispatcher_()};
  }

  MOCK_METHOD0(allocateDispatcher_, Event::Dispatcher*());
  MOCK_METHOD4(createFile,
               Filesystem::FileSharedPtr(const std::string& path, Event::Dispatcher& dispatcher,
                                         Thread::BasicLockable& lock, Stats::Store& stats_store));
  MOCK_METHOD1(fileExists, bool(const std::string& path));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string& path));

  std::shared_ptr<Filesystem::MockFile> file_{new Filesystem::MockFile()};
};

} // namespace Api
} // namespace Envoy
