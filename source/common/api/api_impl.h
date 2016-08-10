#pragma once

#include "envoy/api/api.h"
#include "envoy/filesystem/filesystem.h"

namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api::Api {
public:
  Impl();

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override;
  Filesystem::FilePtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                 Thread::BasicLockable& lock, Stats::Store& stats_store) override;
  bool fileExists(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;

private:
  Filesystem::OsSysCallsPtr os_sys_calls_;
};

} // Api
