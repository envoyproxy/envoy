#include "source/extensions/quic/connection_id_generator/quic_lb/quic_lb.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

namespace {
std::unique_ptr<QuicLbConnectionIdGenerator> createTypedIdGenerator(Factory& factory,
                                                                    uint32_t worker_index = 0) {
  QuicConnectionIdGeneratorPtr generator = factory.createQuicConnectionIdGenerator(worker_index);
  return std::unique_ptr<QuicLbConnectionIdGenerator>(
      static_cast<QuicLbConnectionIdGenerator*>(generator.release()));
}
} // namespace

TEST(QuicLbTest, InvalidConfig) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  // Missing static secret.
  cfg.mutable_encryption_parameters()->set_name("test");
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(), "invalid encryption_parameters config");

  // Missing encryption key.
  envoy::extensions::transport_sockets::tls::v3::Secret encryption_parameters;
  encryption_parameters.set_name("test");
  encryption_parameters.mutable_generic_secret();
  auto status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(), "Missing 'encryption_key'");

  // Invalid key length.
  auto& secrets = *encryption_parameters.mutable_generic_secret()->mutable_secrets();
  envoy::config::core::v3::DataSource key;
  key.set_inline_string("bad");
  secrets["encryption_key"] = key;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "'encryption_key' length was 3, but it must be length 16");

  // Missing version.
  key.set_inline_string("0123456789abcdef");
  secrets["encryption_key"] = key;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(), "Missing 'configuration_version'");

  // Bad version length.
  envoy::config::core::v3::DataSource version;
  std::string version_bytes;
  version_bytes.resize(2);
  version.set_inline_bytes(version_bytes);
  secrets["configuration_version"] = version;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "'configuration_version' length was 2, but it must be length 1 byte");

  // Bad version value.
  version_bytes.resize(1);
  version_bytes.data()[0] = 7;
  version.set_inline_bytes(version_bytes);
  secrets["configuration_version"] = version;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "'configuration_version' was 7, but must be less than 7");

  // Valid config.
  version_bytes.data()[0] = 6;
  version.set_inline_bytes(version_bytes);
  secrets["configuration_version"] = version;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_TRUE(factory_or_status.ok());

  // Invalid concurrency.
  EXPECT_CALL(factory_context.server_factory_context_.options_, concurrency())
      .WillOnce(testing::Return(257));
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "envoy.quic.connection_id_generator.quic_lb cannot be used "
            "with a concurrency greater than 256");
}

// Validate that the server ID is present in plaintext when `unsafe_unencrypted_testing_mode`
// is enabled.
TEST(QuicLbTest, Unencrypted) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.set_unsafe_unencrypted_testing_mode(true);
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);
  cfg.set_self_encode_length(true); // Results in deterministic output.
  cfg.mutable_encryption_parameters()->set_name("test");

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  envoy::extensions::transport_sockets::tls::v3::Secret encryption_parameters;
  encryption_parameters.set_name("test");
  auto& secrets = *encryption_parameters.mutable_generic_secret()->mutable_secrets();
  envoy::config::core::v3::DataSource key;
  key.set_inline_string("0123456789abcdef");
  secrets["encryption_key"] = key;
  envoy::config::core::v3::DataSource version;
  std::string version_bytes;
  version_bytes.resize(1);
  version_bytes.data()[0] = 0;
  version.set_inline_bytes(version_bytes);
  secrets["configuration_version"] = version;
  factory_context.transport_socket_factory_context_.resetSecretManager();
  auto status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryption_parameters);
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  EXPECT_TRUE(status.ok());
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_TRUE(factory_or_status.ok());
  auto generator = createTypedIdGenerator(*factory_or_status.value());
  EXPECT_TRUE(generator->ready());
  auto new_cid = generator->GenerateNextConnectionId(quic::QuicConnectionId{});
  EXPECT_TRUE(new_cid.has_value());
  uint8_t expected[1 + sizeof(id_data)];
  expected[0] = 16; // Configured length of encoded portion of CID. Zero version means the high bits
                    // are all unset.
  memcpy(expected + 1, id_data, sizeof(id_data));
  ASSERT_GT(new_cid->length(), sizeof(expected));

  // First bytes should be the version followed by unencrypted server ID.
  EXPECT_EQ(absl::Span(reinterpret_cast<const uint8_t*>(new_cid->data()), sizeof(expected)),
            absl::Span(expected, sizeof(expected)));
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
