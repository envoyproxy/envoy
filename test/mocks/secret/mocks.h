#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/secret/secret.h"

#include "common/secret/secret_manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Secret {

class MockSecretManager : public SecretManager {
public:
  MockSecretManager();
  ~MockSecretManager();

  MOCK_METHOD1(addOrUpdateStaticSecret, bool(const SecretPtr secret));
  MOCK_METHOD1(getStaticSecret, SecretPtr(const std::string& name));
};

} // namespace Secret
  // namespace Secret
} // namespace Envoy
