#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats.h"

#include "absl/functional/any_invocable.h"
#include "quiche/quic/core/quic_packet_writer.h"

namespace Envoy {
namespace Quic {

using QuicPacketWriterPtr = std::unique_ptr<quic::QuicPacketWriter>;

/**
 * A factory for creating QuicPacketWriters.
 */
class QuicPacketWriterFactory {
public:
  virtual ~QuicPacketWriterFactory() = default;

  /**
   * Creates a QuicPacketWriter.
   * @param io_handle the IoHandle to write to.
   * @param scope the stats scope to write stats to.
   * @param dispatcher the dispatcher for the thread.
   * @param on_can_write_cb callback to invoke when the writer becomes writable.
   * @return the QuicPacketWriter created.
   */
  virtual QuicPacketWriterPtr
  createQuicPacketWriter(Network::IoHandle& io_handle, Stats::Scope& scope,
                         Event::Dispatcher& dispatcher,
                         absl::AnyInvocable<void() &&> on_can_write_cb) PURE;
};

using QuicPacketWriterFactoryPtr = std::unique_ptr<QuicPacketWriterFactory>;

/**
 * QuicPacketWriterFactoryFactory adds an extra layer of indirection In order to
 * support a QuicPacketWriterFactory whose behavior depends on the
 * TypedConfig for that factory.
 */
class QuicPacketWriterFactoryFactory : public Envoy::Config::TypedFactory {
public:
  ~QuicPacketWriterFactoryFactory() override = default;

  /**
   * Creates a QuicPacketWriterFactory based on the specified config.
   * @return the QuicPacketWriterFactory created.
   */
  virtual QuicPacketWriterFactoryPtr
  createQuicPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig& config,
                                Server::Configuration::ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.quic.packet_writer"; }
};

} // namespace Quic
} // namespace Envoy
