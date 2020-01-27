#include "common/tcp_proxy/upstream.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace TcpProxy {
namespace {

class UpstreamTest : public testing::TestWithParam<bool> {
public:
  UpstreamTest() : tcp_upstream_(GetParam()) {
    if (tcp_upstream_) {
    } else {
    }
  }

  bool tcp_upstream_;
  std::unique_ptr<GenericUpstream> upstream_;
};

TEST_P(UpstreamTest, TestBasic) {}

INSTANTIATE_TEST_SUITE_P(UpstreamTypes, UpstreamTest, testing::ValuesIn({true, false}));

} // namespace
} // namespace TcpProxy
} // namespace Envoy
