#pragma once

#include "extensions/filters/common/rbac/engine.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class MockEngine : public RoleBasedAccessControlEngine {
public:
  MockEngine();
  virtual ~MockEngine();

  MOCK_CONST_METHOD2(allowed,
                     bool(const Envoy::Network::Connection&, const Envoy::Http::HeaderMap&));
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
