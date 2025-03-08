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

const absl::string_view kSecretName = "test";

std::unique_ptr<QuicLbConnectionIdGenerator> createTypedIdGenerator(Factory& factory,
                                                                    uint32_t worker_index = 0) {
  QuicConnectionIdGeneratorPtr generator = factory.createQuicConnectionIdGenerator(worker_index);
  return std::unique_ptr<QuicLbConnectionIdGenerator>(
      static_cast<QuicLbConnectionIdGenerator*>(generator.release()));
}

envoy::extensions::transport_sockets::tls::v3::Secret
encryptionParamaters(uint8_t version_int = 0, std::string key_str = "0123456789abcdef") {
  envoy::extensions::transport_sockets::tls::v3::Secret encryption_parameters;
  encryption_parameters.set_name(kSecretName);
  auto& secrets = *encryption_parameters.mutable_generic_secret()->mutable_secrets();
  envoy::config::core::v3::DataSource key;
  key.set_inline_string(key_str);
  secrets["encryption_key"] = key;
  envoy::config::core::v3::DataSource version;
  std::string version_bytes;
  version_bytes.resize(1);
  version_bytes.data()[0] = version_int;
  version.set_inline_bytes(version_bytes);
  secrets["configuration_version"] = version;

  return encryption_parameters;
}

} // namespace

TEST(QuicLbTest, InvalidConfig) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  // Missing static secret.
  cfg.mutable_encryption_parameters()->set_name(kSecretName);
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(), "invalid encryption_parameters config");

  // Missing encryption key.
  envoy::extensions::transport_sockets::tls::v3::Secret encryption_parameters;
  encryption_parameters.set_name(kSecretName);
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

  // Server ID length mismatch
  cfg.set_expected_server_id_length(3);
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "'expected_server_id_length' 3 does not match actual 'server_id' length 6");

  // Valid config with expected length set.
  cfg.set_expected_server_id_length(6);
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_TRUE(factory_or_status.ok());

  // Invalid concurrency.
  EXPECT_CALL(factory_context.server_factory_context_.options_, concurrency())
      .WillOnce(testing::Return(257))
      .WillRepeatedly(testing::Return(8)); // To make subsequent tests pass.
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(factory_or_status.status().message(),
            "envoy.quic.connection_id_generator.quic_lb cannot be used "
            "with a concurrency greater than 256");

  // Invalid combined length.
  cfg.set_nonce_length_bytes(12);
  cfg.mutable_server_id()->set_inline_string("1234567");
  cfg.set_expected_server_id_length(0);
  factory_or_status = Factory::create(cfg, factory_context);
  EXPECT_EQ(
      factory_or_status.status().message(),
      "'server_id' length (7) and 'nonce_length_bytes' (12) combined must be 18 bytes or less.");
}

// Validate that the server ID is present in plaintext when `unsafe_unencrypted_testing_mode`
// is enabled.
TEST(QuicLbTest, Unencrypted) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.set_unsafe_unencrypted_testing_mode(true);
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);
  cfg.mutable_encryption_parameters()->set_name(kSecretName);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  auto status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryptionParamaters(0));
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  auto generator = createTypedIdGenerator(*factory_or_status.value());
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

TEST(QuicLbTest, TooLong) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);
  cfg.mutable_encryption_parameters()->set_name(kSecretName);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  auto status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryptionParamaters(0));
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  auto generator = createTypedIdGenerator(*factory_or_status.value());
  quic::QuicConnectionId id;
  id.set_length(21);
  EXPECT_ENVOY_BUG(generator->appendRoutingId(id), "Connection id long");
}

TEST(QuicLbTest, WorkerSelector) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);
  cfg.mutable_encryption_parameters()->set_name(kSecretName);

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  auto status = factory_context.transport_socket_factory_context_.secretManager().addStaticSecret(
      encryptionParamaters(0));
  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  constexpr uint32_t concurrency = 8;
  QuicConnectionIdWorkerSelector selector =
      factory_or_status.value()->getCompatibleConnectionIdWorkerSelector(concurrency);

  Buffer::OwnedImpl buffer;

  // Packet too short.
  buffer.add(std::string(8, 0));
  const uint32_t default_value = 42;
  EXPECT_EQ(default_value, selector(buffer, default_value));

  // Long header too short.
  buffer = Buffer::OwnedImpl();
  buffer.add(std::string(13, 0));
  uint8_t* buf = reinterpret_cast<uint8_t*>(buffer.linearize(buffer.length()));
  buf[0] = 0x80; // Long header
  buf[5] = 20;   // `DCID` length
  EXPECT_EQ(default_value, selector(buffer, default_value));

  // Long header: packet shorter than encoded CID length.
  buffer.add(std::string(5, 0));
  EXPECT_EQ(default_value, selector(buffer, default_value));

  // Long header: success.
  buffer = Buffer::OwnedImpl();
  buffer.add(std::string(32, 0));
  buf = reinterpret_cast<uint8_t*>(buffer.linearize(buffer.length()));
  buf[0] = 0x80; // Long header
  buf[5] = 8;    // `DCID` length
  buf[5 + 8] = (4 * concurrency) + 3;
  EXPECT_EQ(3, selector(buffer, default_value));

  // Short header: too short.
  buffer = Buffer::OwnedImpl();
  buffer.add(std::string(12, 0));
  buf = reinterpret_cast<uint8_t*>(buffer.linearize(buffer.length()));
  buf[0] = 0x00; // Short header
  buf[1] = 12;   // Encoded length.
  EXPECT_EQ(default_value, selector(buffer, default_value));

  // Short header: invalid concurrency.
  buffer = Buffer::OwnedImpl();
  buffer.add(std::string(12, 0));
  buf = reinterpret_cast<uint8_t*>(buffer.linearize(buffer.length()));
  buf[0] = 0x00;                    // Short header
  buf[1] = 8;                       // Encoded length.
  buf[1 + 8 + 1] = concurrency + 1; // Worker ID suffix.
  EXPECT_EQ(default_value, selector(buffer, default_value));

  // Short header: valid.
  buffer = Buffer::OwnedImpl();
  buffer.add(std::string(12, 0));
  buf = reinterpret_cast<uint8_t*>(buffer.linearize(buffer.length()));
  buf[0] = 0x00;      // Short header
  buf[1] = 8;         // Encoded length.
  buf[1 + 8 + 1] = 3; // Worker ID suffix.
  EXPECT_EQ(3, selector(buffer, default_value));
}

TEST(QuicLbTest, EmptySecretCallback) {
  uint8_t id_data[] = {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56};
  envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config cfg;
  cfg.mutable_server_id()->set_inline_bytes(id_data, sizeof(id_data));
  cfg.set_nonce_length_bytes(10);
  cfg.mutable_encryption_parameters()->set_name(kSecretName);
  cfg.mutable_encryption_parameters()->mutable_sds_config();

  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  auto secret_mgr_unique = std::make_unique<Secret::MockSecretManager>();
  Secret::MockSecretManager* secret_manager = secret_mgr_unique.get();
  factory_context.transport_socket_factory_context_.secret_manager_ = std::move(secret_mgr_unique);
  auto secret_provider = std::make_shared<Secret::MockGenericSecretConfigProvider>();

  EXPECT_CALL(*secret_manager, findOrCreateGenericSecretProvider(_, _, _, _))
      .WillOnce(testing::Return(secret_provider));

  EXPECT_CALL(*secret_provider, addValidationCallback(_));
  std::function<absl::Status()> update_callback;
  EXPECT_CALL(*secret_provider, addUpdateCallback(_))
      .WillOnce(testing::Invoke([&](std::function<absl::Status()> cb) {
        update_callback = cb;
        return nullptr;
      }));
  EXPECT_CALL(*secret_provider, secret()).WillRepeatedly(testing::Return(nullptr));

  absl::StatusOr<std::unique_ptr<Factory>> factory_or_status =
      Factory::create(cfg, factory_context);
  EXPECT_TRUE(factory_or_status.ok());

  auto status = update_callback();
  EXPECT_EQ(status.message(), "secret update callback called with empty secret");
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
