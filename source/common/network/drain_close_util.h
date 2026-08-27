#pragma once

#include <optional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Network {

/**
 * Computes whether a connection should now be drain-closed.
 *
 * Two independent conditions can trigger a drain close:
 *
 * 1. The server is failing health checks (/healthcheck/fail) and the listener's drain type is
 *    DEFAULT. This is deliberately evaluated as a poll rather than from @param drain_event,
 *    because /healthcheck/ok reverses it, and a one-shot drain notification cannot be revoked.
 *
 * 2. The connection has been notified of a drain sequence via Network::Connection::onDrain(). For
 *    DrainStrategy::Immediate this returns true as soon as the notification has been received. For
 *    DrainStrategy::Gradual the probability of returning true ramps linearly from 0 to 1 over the
 *    drain window: P(true) = elapsed / Server::Options::drainTime().
 *
 * @param context supplies the server context, used for the health check state, the configured
 *        drain time, the time source and the random generator.
 * @param drain_type supplies the drain type of the listener owning the connection, from
 *        Network::ListenerInfo::drainType(). For a downstream connection this is reached via
 *        Connection::connectionInfoProvider().listenerInfo().
 * @param drain_event supplies the drain sequence the connection has been notified of, or
 *        std::nullopt if it has not been notified of one.
 */
bool shouldDrainClose(Server::Configuration::ServerFactoryContext& context,
                      envoy::config::listener::v3::Listener::DrainType drain_type,
                      std::optional<ConnectionDrainEvent> drain_event);

} // namespace Network
} // namespace Envoy
