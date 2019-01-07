#include <memory>
#include <string>

#include "envoy/config/quic_listener/quiche/v2alpha/quiche.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/quic/config.h"

#include "extensions/quic_listeners/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

class QuicheListener : public Quic::QuicListener {
public:
  void disable() override {}
  void enable() override {}
};

class QuicheListenerFactory : public Quic::QuicListenerFactory {
public:
  Quic::QuicListenerPtr createQuicListener(Quic::QuicListenerCallbacks&) override {
    return std::make_unique<QuicheListener>();
  }
};

/**
 * Config registration for the QUICHE QUIC listener. @see QuicListenerConfigFactory.
 */
class QuicheListenerConfigFactory : public Quic::QuicListenerConfigFactory {
public:
  // QuicListenerConfigFactory
  Quic::QuicListenerFactoryPtr
  createListenerFactoryFromProto(const Protobuf::Message&,
                                 Server::Configuration::ListenerFactoryContext&) override {
    return std::make_unique<QuicheListenerFactory>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::quic_listener::quiche::v2alpha::Quiche>();
  }

  std::string name() override { return QuicListenerNames::get().Quiche; }
};

/**
 * Static registration for the QUICHE QUIC listener. @see RegisterFactory.
 */
static Registry::RegisterFactory<QuicheListenerConfigFactory, Quic::QuicListenerConfigFactory>
    registered_;

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
