#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Filesystem {

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher();

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // Filesystem
