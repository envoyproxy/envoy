#include "common/redis/command_splitter_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/redis/supported_commands.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Redis {
namespace CommandSplitter {

RespValuePtr Utility::makeError(const std::string& error) {
  RespValuePtr response(new RespValue());
  response->type(RespType::Error);
  response->asString() = error;
  return response;
}
  
InstanceImpl::InstanceImpl(ConnPool::InstancePtr&& conn_pool, Stats::Scope& scope,
                           const std::string& stat_prefix)
    : conn_pool_(std::move(conn_pool)), simple_command_handler_(*conn_pool_),
      mget_handler_(*conn_pool_), stats_{ALL_COMMAND_SPLITTER_STATS(
                                      POOL_COUNTER_PREFIX(scope, stat_prefix + "splitter."))} {
  // TODO(mattklein123) PERF: Make this a trie (like in header_map_impl).
  for (const std::string& command : SupportedCommands::allToOneCommands()) {
    addHandler(scope, stat_prefix, command, simple_command_handler_);
  }

  // TODO(danielhochman): support for other multi-shard operations (del, mset)
  addHandler(scope, stat_prefix, "mget", mget_handler_);
}

SplitRequestPtr InstanceImpl::makeRequest(const RespValue& request, SplitCallbacks& callbacks) {
  if (request.type() != RespType::Array || request.asArray().size() < 2) {
    onInvalidRequest(callbacks);
    return nullptr;
  }

  for (const RespValue& value : request.asArray()) {
    if (value.type() != RespType::BulkString) {
      onInvalidRequest(callbacks);
      return nullptr;
    }
  }

  std::string to_lower_string(request.asArray()[0].asString());
  to_lower_table_.toLowerCase(to_lower_string);

  auto handler = command_map_.find(to_lower_string);
  if (handler == command_map_.end()) {
    stats_.unsupported_command_.inc();
    callbacks.onResponse(Utility::makeError(
        fmt::format("unsupported command '{}'", request.asArray()[0].asString())));
    return nullptr;
  }

  ENVOY_LOG(debug, "redis: splitting '{}'", request.toString());
  handler->second.total_.inc();
  return handler->second.handler_.get().startRequest(request, callbacks);
}

void InstanceImpl::onInvalidRequest(SplitCallbacks& callbacks) {
  stats_.invalid_request_.inc();
  callbacks.onResponse(Utility::makeError("invalid request"));
}

void InstanceImpl::addHandler(Stats::Scope& scope, const std::string& stat_prefix,
                              const std::string& name, CommandHandler& handler) {
  std::string to_lower_name(name);
  to_lower_table_.toLowerCase(to_lower_name);
  command_map_.emplace(
      to_lower_name,
      HandlerData{scope.counter(fmt::format("{}command.{}.total", stat_prefix, to_lower_name)),
                  handler});
}

} // namespace CommandSplitter
} // namespace Redis
} // namespace Envoy
