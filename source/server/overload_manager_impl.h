#pragma once

#include <chrono>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Server {

class OverloadAction {
public:
  OverloadAction(const envoy::config::overload::v3::OverloadAction& config,
                 Stats::Scope& stats_scope);

  // Updates the current pressure for the given resource and returns whether the action
  // has changed state.
  bool updateResourcePressure(const std::string& name, double pressure);

  // Returns the current action state, which is the max state across all registered triggers.
  OverloadActionState getState() const;

  class Trigger {
  public:
    virtual ~Trigger() = default;

    // Updates the current value of the metric and returns whether the trigger has changed state.
    virtual bool updateValue(double value) PURE;

    // Returns the action state for the trigger.
    virtual OverloadActionState actionState() const PURE;
  };
  using TriggerPtr = std::unique_ptr<Trigger>;

private:
  absl::node_hash_map<std::string, TriggerPtr> triggers_;
  OverloadActionState state_;
  Stats::Gauge& active_gauge_;
  Stats::Gauge& scale_percent_gauge_;
};

// Simple table that converts strings into Symbol instances. Symbols are guaranteed to start at 0
// and be indexed sequentially.
class NamedOverloadActionSymbolTable {
public:
  class Symbol {
  public:
    // Allow copy construction everywhere.
    Symbol(const Symbol&) = default;

    // Returns the index of the symbol in the table.
    size_t index() const { return index_; }

    // Support use as a map key.
    bool operator==(const Symbol other) { return other.index_ == index_; }

    // Support absl::Hash.
    template <typename H>
    friend H AbslHashValue(H h, const Symbol& s) { // NOLINT(readability-identifier-naming)
      return H::combine(std::move(h), s.index_);
    }

  private:
    friend class NamedOverloadActionSymbolTable;
    // Only the symbol table class can create Symbol instances from indices.
    explicit Symbol(size_t index) : index_(index) {}

    size_t index_;
  };

  // Finds an existing or adds a new entry for the given name.
  Symbol get(absl::string_view name);

  // Returns the symbol for the name if there is one, otherwise nullopt.
  absl::optional<Symbol> lookup(absl::string_view string) const;

  // Translates a symbol back into a name.
  const absl::string_view name(Symbol symbol) const;

  // Returns the number of symbols in the table. All symbols are guaranteed to have an index less
  // than size().
  size_t size() const { return table_.size(); }

private:
  absl::flat_hash_map<std::string, size_t> table_;
  std::vector<std::string> names_;
};

class OverloadManagerImpl : Logger::Loggable<Logger::Id::main>, public OverloadManager {
public:
  OverloadManagerImpl(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                      ThreadLocal::SlotAllocator& slot_allocator,
                      const envoy::config::overload::v3::OverloadManager& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Server::OverloadManager
  void start() override;
  bool registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                         OverloadActionCb callback) override;
  ThreadLocalOverloadState& getThreadLocalOverloadState() override;

  // Stop the overload manager timer and wait for any pending resource updates to complete.
  // After this returns, overload manager clients should not receive any more callbacks
  // about overload state changes.
  void stop();

private:
  using FlushEpochId = uint64_t;
  class Resource : public ResourceMonitor::Callbacks {
  public:
    Resource(const std::string& name, ResourceMonitorPtr monitor, OverloadManagerImpl& manager,
             Stats::Scope& stats_scope);

    // ResourceMonitor::Callbacks
    void onSuccess(const ResourceUsage& usage) override;
    void onFailure(const EnvoyException& error) override;

    void update(FlushEpochId flush_epoch);

  private:
    const std::string name_;
    ResourceMonitorPtr monitor_;
    OverloadManagerImpl& manager_;
    bool pending_update_;
    FlushEpochId flush_epoch_;
    Stats::Gauge& pressure_gauge_;
    Stats::Counter& failed_updates_counter_;
    Stats::Counter& skipped_updates_counter_;
  };

  struct ActionCallback {
    ActionCallback(Event::Dispatcher& dispatcher, OverloadActionCb callback)
        : dispatcher_(dispatcher), callback_(callback) {}
    Event::Dispatcher& dispatcher_;
    OverloadActionCb callback_;
  };

  void updateResourcePressure(const std::string& resource, double pressure,
                              FlushEpochId flush_epoch);
  // Flushes any enqueued action state updates to all worker threads.
  void flushResourceUpdates();

  bool started_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotPtr tls_;
  NamedOverloadActionSymbolTable action_symbol_table_;
  const std::chrono::milliseconds refresh_interval_;
  Event::TimerPtr timer_;
  absl::node_hash_map<std::string, Resource> resources_;
  absl::node_hash_map<NamedOverloadActionSymbolTable::Symbol, OverloadAction> actions_;

  absl::flat_hash_map<NamedOverloadActionSymbolTable::Symbol, OverloadActionState>
      state_updates_to_flush_;
  absl::flat_hash_map<ActionCallback*, OverloadActionState> callbacks_to_flush_;
  FlushEpochId flush_epoch_ = 0;
  uint64_t flush_awaiting_updates_ = 0;

  using ResourceToActionMap =
      std::unordered_multimap<std::string, NamedOverloadActionSymbolTable::Symbol>;
  ResourceToActionMap resource_to_actions_;

  using ActionToCallbackMap =
      std::unordered_multimap<NamedOverloadActionSymbolTable::Symbol, ActionCallback,
                              absl::Hash<NamedOverloadActionSymbolTable::Symbol>>;
  ActionToCallbackMap action_to_callbacks_;
};

} // namespace Server
} // namespace Envoy
