#pragma once

#include <optional>

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Network {

/**
 * @param connection supplies the connection whose owning listener is queried.
 * @return the drain type of the listener that owns @param connection, or DEFAULT if the connection
 *         has no listener info. The latter is the case for connections that were not accepted by a
 *         network listener, such as API listener connections, and DEFAULT matches the proto default
 *         that a configured listener would have.
 */
envoy::config::listener::v3::Listener::DrainType listenerDrainType(const Connection& connection);

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
 * @param drain_type supplies the drain type of the listener owning the connection.
 * @param drain_event supplies the drain sequence the connection has been notified of, or
 *        std::nullopt if it has not been notified of one.
 */
bool shouldDrainClose(Server::Configuration::ServerFactoryContext& context,
                      envoy::config::listener::v3::Listener::DrainType drain_type,
                      std::optional<ConnectionDrainEvent> drain_event);

} // namespace Network
} // namespace Envoy
