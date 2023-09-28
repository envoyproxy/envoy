#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator.h"

#include "test/extensions/quic/connection_id_generator/matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

using Matcher::FactoryFunctions;
using Matcher::GivenPacket;
using ::quic::QuicConnectionId;
using ::quic::test::QuicTest;
using ::quic::test::TestConnectionId;
using ::quic::test::TestConnectionIdNineBytesLong;

class EnvoyDeterministicConnectionIdGeneratorTest : public QuicTest {
public:
  EnvoyDeterministicConnectionIdGeneratorTest()
      : generator_(EnvoyDeterministicConnectionIdGenerator(connection_id_length_)) {}

protected:
  int connection_id_length_{12};
  EnvoyDeterministicConnectionIdGenerator generator_;
};

TEST_F(EnvoyDeterministicConnectionIdGeneratorTest, NextConnectionIdIsDeterministic) {
  // Verify that two equal connection IDs get the same replacement.
  QuicConnectionId connection_id64a = TestConnectionId(33);
  QuicConnectionId connection_id64b = TestConnectionId(33);
  EXPECT_EQ(connection_id64a, connection_id64b);
  EXPECT_EQ(*generator_.GenerateNextConnectionId(connection_id64a),
            *generator_.GenerateNextConnectionId(connection_id64b));
  QuicConnectionId connection_id72a = TestConnectionIdNineBytesLong(42);
  QuicConnectionId connection_id72b = TestConnectionIdNineBytesLong(42);
  EXPECT_EQ(connection_id72a, connection_id72b);
  EXPECT_EQ(*generator_.GenerateNextConnectionId(connection_id72a),
            *generator_.GenerateNextConnectionId(connection_id72b));
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorTest, NextConnectionIdLengthIsCorrect) {
  // Verify that all generated IDs are of the correct length.
  const char connection_id_bytes[255] = {};
  for (uint8_t i = 0; i < sizeof(connection_id_bytes) - 1; ++i) {
    QuicConnectionId connection_id(connection_id_bytes, i);
    absl::optional<QuicConnectionId> replacement_connection_id =
        generator_.GenerateNextConnectionId(connection_id);
    ASSERT_TRUE(replacement_connection_id.has_value());
    EXPECT_EQ(connection_id_length_, replacement_connection_id->length());
  }
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorTest, NextConnectionIdHasEntropy) {
  // Make sure all these test connection IDs have different replacements.
  for (uint64_t i = 0; i < 256; ++i) {
    QuicConnectionId connection_id_i = TestConnectionId(i);
    absl::optional<QuicConnectionId> new_i = generator_.GenerateNextConnectionId(connection_id_i);
    ASSERT_TRUE(new_i.has_value());
    EXPECT_NE(connection_id_i, *new_i);
    for (uint64_t j = i + 1; j <= 256; ++j) {
      QuicConnectionId connection_id_j = TestConnectionId(j);
      EXPECT_NE(connection_id_i, connection_id_j);
      absl::optional<QuicConnectionId> new_j = generator_.GenerateNextConnectionId(connection_id_j);
      ASSERT_TRUE(new_j.has_value());
      EXPECT_NE(*new_i, *new_j);
    }
  }
}

static uint32_t workerIdFromConnId(QuicConnectionId& id) {
  return absl::little_endian::Load32(id.data());
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorTest, NextConnectionIdPersistsWorkerThreadBytes) {
  for (uint64_t i = 0; i < 256; ++i) {
    QuicConnectionId id = TestConnectionId(i << 16);
    auto next_id = generator_.GenerateNextConnectionId(id);
    ASSERT_TRUE(next_id.has_value());
    EXPECT_EQ(workerIdFromConnId(next_id.value()), workerIdFromConnId(id))
        << "next_id = " << next_id.value() << ", id = " << id;
  }
}

class EnvoyDeterministicConnectionIdGeneratorFactoryTest : public ::testing::Test {
protected:
  EnvoyDeterministicConnectionIdGeneratorFactory factory_;
};

TEST_F(EnvoyDeterministicConnectionIdGeneratorFactoryTest,
       ConnectionIdWorkerSelectorReturnsCurrentWorkerForShortHeaderPacketsTooShort) {
  Buffer::OwnedImpl buffer("aaaaaa");
  EXPECT_THAT(FactoryFunctions(factory_, 256), GivenPacket(buffer).ReturnsDefaultWorkerId());
  EXPECT_THAT(FactoryFunctions(factory_, 65536), GivenPacket(buffer).ReturnsDefaultWorkerId());
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorFactoryTest,
       ConnectionIdWorkerSelectorReturnsCurrentWorkerForLongHeaderPacketsTooShort) {
  Buffer::OwnedImpl buffer("\x80xxxxxxxxxxx");
  EXPECT_THAT(FactoryFunctions(factory_, 256), GivenPacket(buffer).ReturnsDefaultWorkerId());
  EXPECT_THAT(FactoryFunctions(factory_, 65536), GivenPacket(buffer).ReturnsDefaultWorkerId());
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorFactoryTest,
       ConnectionIdWorkerSelectorReturnsBytesOneToFourModConcurrencyForShortPackets) {
  Buffer::OwnedImpl buffer("x\x12\x34\x56\x78xxxxxxxxx");
  EXPECT_THAT(FactoryFunctions(factory_, 256), GivenPacket(buffer).ReturnsWorkerId(0x78));
  EXPECT_THAT(FactoryFunctions(factory_, 65536), GivenPacket(buffer).ReturnsWorkerId(0x5678));
}

TEST_F(EnvoyDeterministicConnectionIdGeneratorFactoryTest,
       ConnectionIdWorkerSelectorReturnsBytesSixToNineModConcurrencyForLongPackets) {
  Buffer::OwnedImpl buffer("\x80xxxxx\x12\x34\x56\x78xxxxxxxxx");
  EXPECT_THAT(FactoryFunctions(factory_, 256), GivenPacket(buffer).ReturnsWorkerId(0x78));
  EXPECT_THAT(FactoryFunctions(factory_, 65536), GivenPacket(buffer).ReturnsWorkerId(0x5678));
}

} // namespace Quic
} // namespace Envoy
