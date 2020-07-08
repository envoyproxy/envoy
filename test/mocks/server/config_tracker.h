#pragma once

#include <string>

#include "envoy/server/config_tracker.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
class MockConfigTracker : public ConfigTracker {
public:
  MockConfigTracker();
  ~MockConfigTracker() override;

  struct MockEntryOwner : public EntryOwner {};

  MOCK_METHOD(EntryOwner*, add_, (std::string, Cb));

  // Server::ConfigTracker
  MOCK_METHOD(const CbsMap&, getCallbacksMap, (), (const));
  EntryOwnerPtr add(const std::string& key, Cb callback) override {
    return EntryOwnerPtr{add_(key, std::move(callback))};
  }

  std::unordered_map<std::string, Cb> config_tracker_callbacks_;
};
} // namespace Server
} // namespace Envoy
