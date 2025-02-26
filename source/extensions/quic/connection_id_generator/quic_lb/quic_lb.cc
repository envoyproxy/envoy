#include "source/extensions/quic/connection_id_generator/quic_lb/quic_lb.h"

#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "quiche/quic/load_balancer/load_balancer_encoder.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

QuicLbConnectionIdGenerator::QuicLbConnectionIdGenerator(
    ThreadLocal::TypedSlot<ThreadLocalData>& tls, uint32_t worker_id)
    : tls_slot_(tls), worker_id_(worker_id) {
  ASSERT(worker_id <= UINT8_MAX,
         "worker id constraint should have been validated in Factory::create");
}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::GenerateNextConnectionId(const quic::QuicConnectionId& original) {
  auto& encoder = tls_slot_->encoder_;
  if (!encoder.IsEncoding()) {
    // TODO: metric
    return absl::nullopt;
  }

  // Encoder doesn't allow generating connection IDs when not using encryption to prevent making
  // a new linkable connection ID.
  //
  // This code is unsafe right now and for testing; override that by calling
  // GenerateConnectionId().
  auto new_cid = encoder.GenerateConnectionId();
  return appendRoutingId(new_cid, original);
}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::MaybeReplaceConnectionId(const quic::QuicConnectionId& original,
                                                      const quic::ParsedQuicVersion& version) {
  auto& encoder = tls_slot_->encoder_;
  if (!encoder.IsEncoding()) {
    // TODO: metric
    return absl::nullopt;
  }

  auto new_cid = encoder.MaybeReplaceConnectionId(original, version);
  if (new_cid.has_value()) {
    return appendRoutingId(new_cid.value(), original);
  }

  return absl::nullopt;
}

uint8_t QuicLbConnectionIdGenerator::ConnectionIdLength(uint8_t first_byte) const {
  return tls_slot_->encoder_.ConnectionIdLength(first_byte) + sizeof(WorkerRoutingIdValue);
}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::appendRoutingId(quic::QuicConnectionId& new_connection_id,
                                             const quic::QuicConnectionId& old_connection_id) {
  uint8_t buffer[quic::kQuicMaxConnectionIdWithLengthPrefixLength];

  const uint16_t new_length = new_connection_id.length() + sizeof(WorkerRoutingIdValue);
  if (new_length > sizeof(buffer)) {
    IS_ENVOY_BUG("Invalid encoder configuration led to connection id being too long");
    return {};
  }

  if (old_connection_id.length() < sizeof(WorkerRoutingIdValue)) {
    IS_ENVOY_BUG("Unexpected very short connection id");
    return {};
  }

  memcpy(buffer, new_connection_id.data(), new_connection_id.length());

  WorkerRoutingIdValue* routing_info_destination =
      reinterpret_cast<WorkerRoutingIdValue*>(buffer + new_connection_id.length());

  static_assert(
      sizeof(*routing_info_destination) == sizeof(uint8_t),
      "Below line needs memcpy due to possibly unaligned data if the size is not a single byte");

  // Stamp the id as the trailing byte. This adds a small amount of linkability.
  *routing_info_destination = worker_id_;

  ENVOY_LOG_MISC(trace, "generating new connection id for worker_id {}, len {} {}", worker_id_,
                 new_length,
                 quic::QuicConnectionId(absl::Span<const uint8_t>(buffer, new_length)).ToString());

  ASSERT(tls_slot_->encoder_.IsEncoding());
  return quic::QuicConnectionId(absl::Span<const uint8_t>(buffer, new_length));
}

QuicLbConnectionIdGenerator::ThreadLocalData::ThreadLocalData(
    const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
    absl::string_view server_id)
    : encoder_(*quic::QuicRandom::GetInstance(), nullptr /* visitor */,
               config.self_encode_length()),
      unsafe_unencrypted_testing_mode_(config.unsafe_unencrypted_testing_mode()),
      nonce_length_bytes_(config.nonce_length_bytes()), server_id_(server_id) {}

absl::StatusOr<std::shared_ptr<QuicLbConnectionIdGenerator::ThreadLocalData>>
QuicLbConnectionIdGenerator::ThreadLocalData::create(
    const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
    const absl::string_view server_id) {

  std::shared_ptr<QuicLbConnectionIdGenerator::ThreadLocalData> ret(
      new QuicLbConnectionIdGenerator::ThreadLocalData(config, server_id));

  return ret;
}

absl::Status
QuicLbConnectionIdGenerator::ThreadLocalData::updateKeyAndVersion(const std::string& key,
                                                                  uint8_t version) {
  std::optional<quic::LoadBalancerConfig> lb_config;
  if (unsafe_unencrypted_testing_mode_) {
    lb_config = quic::LoadBalancerConfig::CreateUnencrypted(version, server_id_.length(),
                                                            nonce_length_bytes_);
  } else {
    lb_config =
        quic::LoadBalancerConfig::Create(version, server_id_.length(), nonce_length_bytes_, key);
  }

  if (!lb_config.has_value()) {
    return absl::InvalidArgumentError("error generating quic::LoadBalancerConfig");
  }
  bool success = encoder_.UpdateConfig(lb_config.value(), server_id_);
  if (!success) {
    return absl::InvalidArgumentError("Error setting configuration of quic-lb CID encoder");
  }

  return absl::OkStatus();
}

namespace {

Secret::GenericSecretConfigProviderSharedPtr secretsProvider(
    const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
    Server::Configuration::TransportSocketFactoryContext& transport_socket_factory_context,
    Init::Manager& init_manager) {
  Secret::SecretManager& secret_manager = transport_socket_factory_context.secretManager();
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(
        config.sds_config(), config.name(), transport_socket_factory_context, init_manager);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}

absl::StatusOr<std::pair<std::string, uint8_t>> getAndValidateKeyAndVersion(
    const envoy::extensions::transport_sockets::tls::v3::GenericSecret& secret, Api::Api& api) {
  const auto& secrets = secret.secrets();

  auto key_it = secrets.find("encryption_key");
  if (key_it == secrets.end()) {
    return absl::InvalidArgumentError("Missing 'encryption_key'");
  }

  auto key_or_result = Config::DataSource::read(key_it->second, false, api);
  RETURN_IF_NOT_OK_REF(key_or_result.status());

  std::string key = key_or_result.value();
  if (key.size() != quic::kLoadBalancerKeyLen) {
    return absl::InvalidArgumentError(
        fmt::format("'encryption_key' length was {}, but it must be length {}", key.size(),
                    quic::kLoadBalancerKeyLen));
  }

  auto version_it = secrets.find("configuration_version");
  if (version_it == secrets.end()) {
    return absl::InvalidArgumentError("Missing 'configuration_version'");
  }

  auto version_or_result = Config::DataSource::read(version_it->second, false, api);
  RETURN_IF_NOT_OK_REF(version_or_result.status());

  if (version_or_result.value().size() != sizeof(uint8_t)) {
    return absl::InvalidArgumentError(
        fmt::format("'configuration_version' length was {}, but it must be length 1 byte",
                    version_or_result.value().size()));
  }
  uint8_t version = version_or_result.value().data()[0];
  if (version > quic::kNumLoadBalancerConfigs) {
    return absl::InvalidArgumentError(
        fmt::format("'configuration_version' was {}, but must be less than or equal to {}", version,
                    quic::kNumLoadBalancerConfigs));
  }

  return std::make_pair(key, version);
}

} // namespace

Factory::Factory(
    const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config)
    : config_(config) {}

absl::StatusOr<std::unique_ptr<Factory>>
Factory::create(const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
                Server::Configuration::FactoryContext& context) {
  // The worker ID is stored in a single byte in the connection ID, so restrict use to concurrency
  // values compatible with this scheme.
  // TODO(ggreenway): use additional bytes for higher concurrency.
  if (context.serverFactoryContext().options().concurrency() >= UINT8_MAX) {
    return absl::InvalidArgumentError("envoy.quic.connection_id_generator.quic_lb cannot be used "
                                      "with a concurrency greater than 256");
  }

  std::unique_ptr<Factory> ret(new Factory(config));

  auto server_id_or_result =
      Config::DataSource::read(config.server_id(), false, context.serverFactoryContext().api());
  RETURN_IF_NOT_OK_REF(server_id_or_result.status());

  std::string server_id = server_id_or_result.value();

  // Create a test instance using all the same parameters, but with a fake key (because we don't
  // have the real key yet) to surface any errors while we're still in the config loading stage.
  {
    auto test_instance_or_result =
        QuicLbConnectionIdGenerator::ThreadLocalData::create(config, server_id);
    RETURN_IF_NOT_OK_REF(test_instance_or_result.status());
    std::string test_key(quic::kLoadBalancerKeyLen, '0');
    auto result = test_instance_or_result.value()->updateKeyAndVersion(test_key, 0);
    RETURN_IF_NOT_OK_REF(result);

    // Record the length of the connection ID so that we know the offset to look for
    // routing information.
    ret->connection_id_length_ =
        test_instance_or_result.value()->encoder_.GenerateConnectionId().length() +
        QuicLbConnectionIdGenerator::connectionIdLengthAddition();
  }

  ret->secrets_provider_ =
      secretsProvider(config.encryption_parmeters(), context.getTransportSocketFactoryContext(),
                      context.initManager());
  if (ret->secrets_provider_ == nullptr) {
    return absl::InvalidArgumentError("invalid encryption_parmeters config");
  }

  ret->secrets_provider_validation_callback_handle_ = ret->secrets_provider_->addValidationCallback(
      [&api = context.serverFactoryContext().api()](
          const envoy::extensions::transport_sockets::tls::v3::GenericSecret& secret)
          -> absl::Status { return getAndValidateKeyAndVersion(secret, api).status(); });

  ret->secrets_provider_update_callback_handle_ = ret->secrets_provider_->addUpdateCallback(
      [&factory = *ret, &api = context.serverFactoryContext().api()]() -> absl::Status {
        const envoy::extensions::transport_sockets::tls::v3::GenericSecret* secret =
            factory.secrets_provider_->secret();
        if (secret == nullptr) {
          return absl::NotFoundError("secret update callback called with empty secret");
        }

        auto data_or_result = getAndValidateKeyAndVersion(*secret, api);
        RETURN_IF_NOT_OK_REF(data_or_result.status());

        factory.tls_slot_->runOnAllThreads(
            [data =
                 data_or_result.value()](OptRef<QuicLbConnectionIdGenerator::ThreadLocalData> obj) {
              ASSERT(obj.has_value(),
                     "Guaranteed if `set()` was previously called on the tls slot");

              // TODO: error handling from both of these calls
              auto result = obj->updateKeyAndVersion(data.first, data.second);
              ASSERT(result.ok(), "Parameters and data was already validated");
            });

        return absl::OkStatus();
      });

  ret->tls_slot_ = ThreadLocal::TypedSlot<QuicLbConnectionIdGenerator::ThreadLocalData>::makeUnique(
      context.serverFactoryContext().threadLocal());

  // using InitializeCb = std::function<std::shared_ptr<T>(Event::Dispatcher & dispatcher)>;
  ret->tls_slot_->set(
      [=](Event::Dispatcher&) -> std::shared_ptr<QuicLbConnectionIdGenerator::ThreadLocalData> {
        auto result = QuicLbConnectionIdGenerator::ThreadLocalData::create(config, server_id);
        ASSERT(result.status().ok()); // Configuration was validated above.
        return result.value();
      });

  return ret;
}

QuicConnectionIdGeneratorPtr Factory::createQuicConnectionIdGenerator(uint32_t worker_id) {
  return std::make_unique<QuicLbConnectionIdGenerator>(*tls_slot_, worker_id);
}

Network::Socket::OptionConstSharedPtr
Factory::createCompatibleLinuxBpfSocketOption(uint32_t concurrency) {
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  //
  // TODO(ggreenway): write a BPF filter. Without it, performance will be very poor.
  //
  UNREFERENCED_PARAMETER(concurrency);
  UNREFERENCED_PARAMETER(prog_);
  return nullptr;

#else
  UNREFERENCED_PARAMETER(concurrency);
  return nullptr;
#endif
}

static uint32_t bpfEquivalentFunction(const Buffer::Instance& packet, uint8_t concurrency,
                                      uint32_t default_value, uint8_t connection_id_length) {
  const uint64_t packet_length = packet.length();
  if (packet_length < 9) {
    return default_value;
  }

  uint8_t first_octet;
  packet.copyOut(0, sizeof(first_octet), &first_octet);

  if (first_octet & 0x80) {
    // IETF QUIC long header.
    // The connection id length is the 6th byte.
    // The connection id starts from 7th byte.
    // Minimum length of a long header packet is 14.
    constexpr uint8_t kCIDLenOffset = 5;
    constexpr uint8_t kCIDOffset = 6;

    if (packet_length < 14) {
      return default_value;
    }

    uint8_t connection_id_snippet;

    uint8_t id_len;
    packet.copyOut(kCIDLenOffset, sizeof(id_len), &id_len);

    if (packet_length < (kCIDOffset + id_len)) {
      return default_value;
    }

    packet.copyOut(kCIDOffset + id_len - sizeof(connection_id_snippet),
                   sizeof(connection_id_snippet), &connection_id_snippet);
    ENVOY_LOG_MISC(trace, "long header for worker {}", connection_id_snippet % concurrency);
    return connection_id_snippet % concurrency;
  } else {
    // IETF QUIC short header, or gQUIC.
    // The connection id starts from 2nd byte.
    constexpr uint8_t kCIDOffset = 1;

    WorkerRoutingIdValue worker_id;
    if (packet_length < (kCIDOffset + connection_id_length)) {
      ENVOY_LOG_MISC(debug, "short header too short");
      return default_value;
    }

    packet.copyOut(kCIDOffset + connection_id_length - sizeof(worker_id), sizeof(worker_id),
                   &worker_id);
    if (worker_id >= concurrency) {
      ENVOY_LOG_MISC(debug, "short header unexpected value {} >= {}", worker_id, concurrency);
      return default_value;
    }

    ENVOY_LOG_MISC(trace, "short header for worker {}", worker_id);
    return worker_id;
  }
}

QuicConnectionIdWorkerSelector
Factory::getCompatibleConnectionIdWorkerSelector(uint32_t concurrency) {
  return [concurrency, connection_id_length = connection_id_length_](const Buffer::Instance& packet,
                                                                     uint32_t default_value) {
    return bpfEquivalentFunction(packet, concurrency, default_value, connection_id_length);
  };
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
