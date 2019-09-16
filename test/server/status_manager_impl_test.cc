#include "server/status_manager_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

TEST(StatusManagerImpl, BasicFunctionality) {
  StatusManagerImpl impl;
  impl.addComponent("foo");

  auto status = std::make_unique<StatusManager::StatusHandle>(true, "details");
  impl.updateStatus("foo", std::move(status));

  StatusManager::StatusHandle returned_status = impl.status("foo");
  EXPECT_EQ("details", returned_status.details_);
  EXPECT_EQ(true, returned_status.success_);
}

} // namespace Server
} // namespace Envoy
