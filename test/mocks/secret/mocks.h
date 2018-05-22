#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/secret/secret.h"
#include "envoy/secret/secret_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Secret {

class MockSecretManager : public SecretManager {
public:
  MockSecretManager();
  ~MockSecretManager();

  MOCK_METHOD1(addOrUpdateStaticSecret, bool(const SecretSharedPtr secret));
  MOCK_CONST_METHOD1(staticSecret, const SecretSharedPtr(const std::string& name));
};

} // namespace Secret
  // namespace Secret
} // namespace Envoy
