#include "source/extensions/quic/connection_id_generator/quic_lb/quic_lb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

TEST(QuicLbTest, testing) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  // auto generator = QuicLbConnectionIdGenerator::create(cfg).value();
  // quic::QuicConnectionId old_id("123456789", 9);
  // auto id = generator->GenerateNextConnectionId(old_id);

  // EXPECT_EQ(id, old_id);
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
