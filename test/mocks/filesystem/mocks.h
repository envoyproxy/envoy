#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/common/thread.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Filesystem {

class MockFile : public File {
public:
  MockFile();
  ~MockFile();

  // Filesystem::File
  MOCK_METHOD1(write, void(absl::string_view data));
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD0(flush, void());
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher();

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
