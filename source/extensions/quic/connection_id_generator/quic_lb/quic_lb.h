#pragma once

#include "envoy/extensions/quic/connection_id_generator/quic_lb/v3/quic_lb.pb.h"

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "quiche/quic/load_balancer/load_balancer_encoder.h"

#if defined(__linux__)
#include <linux/filter.h>
#endif

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace QuicLb {

// If this type changes such that more bytes are required, update the docs in quic_lb.proto which
// specifies the maximum length of the server_id and nonce combined.
using WorkerRoutingIdValue = uint8_t;

class QuicLbConnectionIdGenerator : public quic::ConnectionIdGeneratorInterface {
public:
  class ThreadLocalData : public ThreadLocal::ThreadLocalObject {
  public:
    static absl::StatusOr<std::shared_ptr<ThreadLocalData>>
    create(const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
           absl::string_view server_id);

    absl::Status updateKeyAndVersion(absl::string_view key, uint8_t version);

    quic::LoadBalancerEncoder encoder_;

  private:
    ThreadLocalData(
        const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
        absl::string_view server_id);

    const bool unsafe_unencrypted_testing_mode_;
    const uint32_t nonce_length_bytes_;
    const quic::LoadBalancerServerId server_id_;
  };

  QuicLbConnectionIdGenerator(ThreadLocal::TypedSlot<ThreadLocalData>& tls, uint32_t worker_id);

  // quic::ConnectionIdGeneratorInterface
  absl::optional<quic::QuicConnectionId>
  GenerateNextConnectionId(const quic::QuicConnectionId& original) override;
  absl::optional<quic::QuicConnectionId>
  MaybeReplaceConnectionId(const quic::QuicConnectionId& original,
                           const quic::ParsedQuicVersion& version) override;
  uint8_t ConnectionIdLength(uint8_t first_byte) const override;

  absl::optional<quic::QuicConnectionId> appendRoutingId(quic::QuicConnectionId& new_connection_id);

private:
  ThreadLocal::TypedSlot<ThreadLocalData>& tls_slot_;
  const WorkerRoutingIdValue worker_id_;
};

class Factory : public EnvoyQuicConnectionIdGeneratorFactory {
public:
  static absl::StatusOr<std::unique_ptr<Factory>>
  create(const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config,
         Server::Configuration::FactoryContext& context);

  // EnvoyQuicConnectionIdGeneratorFactory.
  QuicConnectionIdGeneratorPtr createQuicConnectionIdGenerator(uint32_t worker_index) override;
  Network::Socket::OptionConstSharedPtr
  createCompatibleLinuxBpfSocketOption(uint32_t concurrency) override;
  QuicConnectionIdWorkerSelector
  getCompatibleConnectionIdWorkerSelector(uint32_t concurrency) override;

private:
  Factory(const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config& config);
  absl::Status updateSecret(Api::Api& api);

  const envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config config_;
  Secret::GenericSecretConfigProviderSharedPtr secrets_provider_;
  Common::CallbackHandlePtr secrets_provider_validation_callback_handle_;
  Common::CallbackHandlePtr secrets_provider_update_callback_handle_;
  ThreadLocal::TypedSlotPtr<QuicLbConnectionIdGenerator::ThreadLocalData> tls_slot_;

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  sock_fprog prog_;
  std::vector<sock_filter> filter_;
#endif
};

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
