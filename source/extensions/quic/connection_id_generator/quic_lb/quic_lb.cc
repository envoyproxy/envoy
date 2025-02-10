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

// ggreenway:
//  For ebpf routing, for long headers use early bytes to hash on, then take those bytes
//  and put them at the end of the CID.
//  For short header, use the bytes at end of CID.

QuicLbConnectionIdGenerator::QuicLbConnectionIdGenerator(
    ThreadLocal::TypedSlot<ThreadLocalData>& tls)
    : tls_slot_(tls) {}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::GenerateNextConnectionId(const quic::QuicConnectionId& original) {
  // encoder_ doesn't allow generating connection IDs when not using encryption to prevent making
  // a new linkable connection ID.
  //
  // This code is unsafe right now and for testing; override that by calling
  // GenerateConnectionId().
  auto new_cid = tls_slot_->encoder_.GenerateConnectionId();
  return appendRoutingId(new_cid, original);
}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::MaybeReplaceConnectionId(const quic::QuicConnectionId& original,
                                                      const quic::ParsedQuicVersion& version) {
  auto new_cid = tls_slot_->encoder_.MaybeReplaceConnectionId(original, version);
  if (new_cid.has_value()) {
    return appendRoutingId(new_cid.value(), original);
  }

  return new_cid;
}

uint8_t QuicLbConnectionIdGenerator::ConnectionIdLength(uint8_t first_byte) const {
  return tls_slot_->encoder_.ConnectionIdLength(first_byte) + kRoutingInfoSize;
}

absl::optional<quic::QuicConnectionId>
QuicLbConnectionIdGenerator::appendRoutingId(quic::QuicConnectionId& new_connection_id,
                                             const quic::QuicConnectionId& old_connection_id) {
  uint8_t buffer[quic::kQuicMaxConnectionIdWithLengthPrefixLength];

  const uint16_t new_length = new_connection_id.length() + kRoutingInfoSize;
  if (new_length > sizeof(buffer)) {
    IS_ENVOY_BUG("Invalid encoder configuration led to connection id being too long");
    return {};
  }

  if (old_connection_id.length() < kRoutingInfoSize) {
    IS_ENVOY_BUG("Unexpected very short connection id");
    return {};
  }

  memcpy(buffer, new_connection_id.data(), new_connection_id.length());

  uint8_t* routing_info_destination = buffer + new_connection_id.length();
  const uint8_t* routing_info_source = reinterpret_cast<const uint8_t*>(old_connection_id.data()) +
                                       old_connection_id.length() - kRoutingInfoSize;
  memcpy(routing_info_destination, routing_info_source, kRoutingInfoSize);

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

  auto key_it = secrets.find("key");
  if (key_it == secrets.end()) {
    return absl::InvalidArgumentError("Missing 'key'");
  }

  auto key_or_result = Config::DataSource::read(key_it->second, false, api);
  RETURN_IF_NOT_OK_REF(key_or_result.status());

  std::string key = key_or_result.value();
  if (key.size() != quic::kLoadBalancerKeyLen) {
    return absl::InvalidArgumentError(fmt::format("'key' length was {}, but it must be length {}",
                                                  key.size(), quic::kLoadBalancerKeyLen));
  }

  auto version_it = secrets.find("version");
  if (version_it == secrets.end()) {
    return absl::InvalidArgumentError("Missing 'version'");
  }

  auto version_or_result = Config::DataSource::read(version_it->second, false, api);
  RETURN_IF_NOT_OK_REF(version_or_result.status());

  if (version_or_result.value().size() != 1) {
    return absl::InvalidArgumentError(fmt::format(
        "'version' length was {}, but it must be length 1", version_or_result.value().size()));
  }
  uint8_t version = version_or_result.value().data()[0];
  if (version > quic::kNumLoadBalancerConfigs) {
    return absl::InvalidArgumentError(
        fmt::format("'version' was {}, but must be less than or equal to {}", version,
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

    // Record the length of the encoder's connection ID so that we know the offset to look for
    // routing information.
    ret->encoder_connection_id_length_ =
        test_instance_or_result.value()->encoder_.GenerateConnectionId().length();
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

QuicConnectionIdGeneratorPtr Factory::createQuicConnectionIdGenerator(uint32_t) {
  return std::make_unique<QuicLbConnectionIdGenerator>(*tls_slot_);
}

Network::Socket::OptionConstSharedPtr
Factory::createCompatibleLinuxBpfSocketOption(uint32_t concurrency) {
  //
  // TODO: adapt the `deterministic` ebpf rule to work with quic-lb
  //
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  // This BPF filter reads the 1st word of QUIC connection id in the UDP payload and mods it by
  // the number of workers to get the socket index in the SO_REUSEPORT socket groups. QUIC packets
  // should be at least 9 bytes, with the 1st byte indicating one of the below QUIC packet
  // headers: 1) IETF QUIC long header: most significant bit is 1. The connection id starts from
  // the 7th byte. 2) IETF QUIC short header: most significant bit is 0. The connection id starts
  // from 2nd byte. 3) Google QUIC header: most significant bit is 0. The connection id starts
  // from 2nd byte. Any packet that doesn't belong to any of the three packet header types are
  // dispatched based on 5-tuple source/destination addresses. SPELLCHECKER(off)
  filter_ = {
      {0x80, 0, 0, 0000000000}, //                   ld len
      {0x35, 0, 9, 0x00000009}, //                   jlt #0x9, packet_too_short
      {0x30, 0, 0, 0000000000}, //                   ldb [0]
      {0x54, 0, 0, 0x00000080}, //                   and #0x80
      {0x15, 0, 2, 0000000000}, //                   jne #0, ietf_long_header
      {0x20, 0, 0, 0x00000001}, //                   ld [1]
      {0x05, 0, 0, 0x00000005}, //                   ja return
      {0x80, 0, 0, 0000000000}, // ietf_long_header: ld len
      {0x35, 0, 2, 0x0000000e}, //                   jlt #0xe, packet_too_short
      {0x20, 0, 0, 0x00000006}, //                   ld [6]
      {0x05, 0, 0, 0x00000001}, //                   ja return
      {0x20, 0, 0,              // packet_too_short: ld rxhash
       static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_RXHASH)},
      {0x94, 0, 0, concurrency}, // return:         mod #socket_count
      {0x16, 0, 0, 0000000000},  //                 ret a
  };
  // SPELLCHECKER(on)

  // Note that this option refers to the BPF program data above, which must live until the
  // option is used. The program is kept as a member variable for this purpose.
  prog_.len = filter_.size();
  prog_.filter = filter_.data();
  return std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_BOUND, ENVOY_ATTACH_REUSEPORT_CBPF,
      absl::string_view(reinterpret_cast<char*>(&prog_), sizeof(prog_)));
#else
  UNREFERENCED_PARAMETER(concurrency);
  PANIC("BPF filter is not supported in this platform.");
#endif
}

//
// TODO: adapt the `deterministic` code to work with quic-lb
//
static uint32_t bpfEquivalentFunction(const Buffer::Instance& packet, uint32_t concurrency,
                                      uint32_t default_value) {
  // This is a re-implementation of the same algorithm written in BPF in
  // createCompatibleLinuxBpfSocketOption
  const uint64_t packet_length = packet.length();
  if (packet_length < 9) {
    return default_value;
  }

  uint8_t first_octet;
  packet.copyOut(0, sizeof(first_octet), &first_octet);

  uint16_t connection_id_snippet;
  if (first_octet & 0x80) {
    // IETF QUIC long header.
    // The connection id starts from 7th byte.
    // Minimum length of a long header packet is 14.
    if (packet_length < 14) {
      return default_value;
    }

    packet.copyOut(6, sizeof(connection_id_snippet), &connection_id_snippet);
  } else {
    // IETF QUIC short header, or gQUIC.
    // The connection id starts from 2nd byte.
    packet.copyOut(1, sizeof(connection_id_snippet), &connection_id_snippet);
  }

  connection_id_snippet = htonl(connection_id_snippet);
  return connection_id_snippet % concurrency;
}

QuicConnectionIdWorkerSelector
Factory::getCompatibleConnectionIdWorkerSelector(uint32_t concurrency) {
  return [concurrency](const Buffer::Instance& packet, uint32_t default_value) {
    return bpfEquivalentFunction(packet, concurrency, default_value);
  };
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
