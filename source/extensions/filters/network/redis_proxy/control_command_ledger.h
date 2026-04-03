#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "envoy/upstream/upstream.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * A per-host FIFO of the fire-and-forget control commands (``SSUBSCRIBE`` / ``SUNSUBSCRIBE``) the
 * subscription registry has sent upstream but not yet seen acked or errored. Redis answers a
 * connection's pipelined control commands IN ORDER, so the head of a host's FIFO is always the
 * oldest command still awaiting a reply. That is how the registry correlates an out-of-band
 * upstream Push (a subscribe/unsubscribe ack) or a non-Push reply (an ``-ERR``) back to the exact
 * (verb, channel, generation) attempt that produced it — the single ledger (A-4/S-1) that replaced
 * the former separate expected-ack bookkeeping.
 *
 * Extracted from SubscriptionRegistry (D3) as an independently unit-testable component. The ledger
 * only orders commands; the registry owns SSUBSCRIBE-attempt GENERATION assignment and passes the
 * generation in, so this class carries no policy — just the FIFO discipline.
 */
class ControlCommandLedger {
public:
  struct PendingControlCommand {
    std::string verb; // "ssubscribe" | "sunsubscribe"
    std::string channel;
    // The SSUBSCRIBE attempt generation this command belongs to (owned by the registry). Lets an
    // ack consumed here in FIFO order name WHICH attempt it acked, so a same-host stale ack (A -> B
    // -> A) is told apart from the current one. 0 for SUNSUBSCRIBE (never generation-correlated).
    uint64_t generation;
  };

  /**
   * Record a control command just sent to ``host`` (appends to the tail). A null host is a no-op:
   * registry unit tests inject acks without a source host, and the ledger is only consulted when
   * the host is known (production).
   */
  void record(const Upstream::HostConstSharedPtr& host, absl::string_view verb,
              const std::string& channel, uint64_t generation = 0) {
    if (host == nullptr) {
      return;
    }
    commands_[host].push_back({std::string(verb), channel, generation});
  }

  /**
   * An in-order ack landed: pop the head IF it is exactly the (``verb``, ``channel``) at the front,
   * returning the generation it acked (so the caller tells the current attempt from a stale one).
   * Returns nullopt on a head mismatch — an out-of-band / unsolicited push, or a null-host test
   * injection — leaving the FIFO intact rather than corrupting the ordering.
   */
  std::optional<uint64_t> consumeAck(const Upstream::HostConstSharedPtr& host,
                                     absl::string_view verb, const std::string& channel) {
    if (host == nullptr) {
      return std::nullopt;
    }
    auto it = commands_.find(host);
    if (it == commands_.end() || it->second.empty()) {
      return std::nullopt;
    }
    const PendingControlCommand& head = it->second.front();
    if (head.verb != verb || head.channel != channel) {
      return std::nullopt;
    }
    const uint64_t generation = head.generation;
    popHead(it);
    return generation;
  }

  /**
   * Take (pop) the oldest outstanding control command on ``host`` — the one a just-received
   * NON-Push reply (an ``-ERR``, or an anomalous non-Error) corresponds to, since Redis answers
   * pipelined control commands in order. Both the error path and the non-Error path consume exactly
   * one head to keep the FIFO in lockstep with the reply stream. nullopt if the host has none /
   * null host.
   */
  std::optional<PendingControlCommand> takeReply(const Upstream::HostConstSharedPtr& host) {
    if (host == nullptr) {
      return std::nullopt;
    }
    auto it = commands_.find(host);
    if (it == commands_.end() || it->second.empty()) {
      return std::nullopt;
    }
    PendingControlCommand head = std::move(it->second.front());
    popHead(it);
    return head;
  }

  /**
   * Drop a host's whole FIFO: its subscription connection died or was retired, so none of its
   * outstanding commands will ever ack. Leaving a stale head behind would head-mismatch (and thus
   * black out) the correlation of every command on that host's NEXT subscribe epoch.
   */
  void clear(const Upstream::HostConstSharedPtr& host) { commands_.erase(host); }

  /**
   * Drop EVERY host's FIFO: the registry's clear() on cluster removal / update, after which no
   * subscription survives, so no outstanding control command is meaningful.
   */
  void clearAll() { commands_.clear(); }

private:
  using Map = absl::flat_hash_map<Upstream::HostConstSharedPtr, std::deque<PendingControlCommand>>;
  void popHead(Map::iterator it) {
    it->second.pop_front();
    if (it->second.empty()) {
      commands_.erase(it); // keep the map free of empty deques so ``find`` == "has outstanding"
    }
  }
  Map commands_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
