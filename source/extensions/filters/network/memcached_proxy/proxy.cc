#include "extensions/filters/network/memcached_proxy/proxy.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

ProxyFilter::ProxyFilter(
  const std::string& stat_prefix,
  Stats::Scope& scope,
  // Runtime::Loader& runtime,
  // const Network::DrainDecision& drain_decision,
  // Runtime::RandomGenerator& generator,
  // TimeSource& time_source,
  DecoderFactory& factory,
  EncoderPtr&& encoder)
    : stat_prefix_(stat_prefix),
    // scope_(scope),
    stats_(generateStats(stat_prefix, scope)),
      // runtime_(runtime),
      // drain_decision_(drain_decision),
      // generator_(generator),
       // time_source_(time_source),
       decoder_(factory.create(*this)), encoder_(std::move(encoder))
       {
}

void ProxyFilter::decodeGet(GetRequestPtr&& message) {
  stats_.op_get_.inc();
  ENVOY_LOG(trace, "decoded `GET` key={}", message->key());
}

void ProxyFilter::decodeGetk(GetkRequestPtr&& message) {
  stats_.op_getk_.inc();
  ENVOY_LOG(trace, "decoded `GETK` key={}", message->key());
}

void ProxyFilter::decodeDelete(DeleteRequestPtr&& message) {
  stats_.op_delete_.inc();
  ENVOY_LOG(trace, "decoded `DELETE` key={}", message->key());
}

void ProxyFilter::decodeSet(SetRequestPtr&& message) {
  stats_.op_set_.inc();
  ENVOY_LOG(trace, "decoded `SET` key={}, body={}", message->key(), message->body());
}

void ProxyFilter::decodeAdd(AddRequestPtr&& message) {
  stats_.op_add_.inc();
  ENVOY_LOG(trace, "decoded `ADD` key={}, body={}", message->key(), message->body());
}

void ProxyFilter::decodeReplace(ReplaceRequestPtr&& message) {
  stats_.op_replace_.inc();
  ENVOY_LOG(trace, "decoded `REPLACE` key={}, body={}", message->key(), message->body());
}

void ProxyFilter::decodeIncrement(IncrementRequestPtr&& message) {
  stats_.op_increment_.inc();
  ENVOY_LOG(trace, "decoded `INCREMENT` key={}, amount={}, initial_value={}", message->key(), message->amount(), message->initialValue());
}

void ProxyFilter::decodeDecrement(DecrementRequestPtr&& message) {
  stats_.op_decrement_.inc();
  ENVOY_LOG(trace, "decoded `DECREMENT` key={}, amount={}, initial_value={}", message->key(), message->amount(), message->initialValue());
}

void ProxyFilter::decodeAppend(AppendRequestPtr&& message) {
  stats_.op_append_.inc();
  ENVOY_LOG(trace, "decoded `APPEND` key={}, body={}", message->key(), message->body());
}

void ProxyFilter::decodePrepend(PrependRequestPtr&& message) {
  stats_.op_prepend_.inc();
  ENVOY_LOG(trace, "decoded `PREPEND` key={}, body={}", message->key(), message->body());
}

void ProxyFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (drain_close_timer_) {
      drain_close_timer_->disableTimer();
      drain_close_timer_.reset();
    }
  }

  // if (event == Network::ConnectionEvent::RemoteClose && !active_query_list_.empty()) {
  //   stats_.cx_destroy_local_with_active_rq_.inc();
  // }

  // if (event == Network::ConnectionEvent::LocalClose && !active_query_list_.empty()) {
  //   stats_.cx_destroy_remote_with_active_rq_.inc();
  // }
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data, bool) {
  read_buffer_.add(data);

  try {
    decoder_->onData(read_buffer_);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "memcached decoding error: {}", e.what());
    stats_.decoding_error_.inc();
  }

  return Network::FilterStatus::Continue;
}

Network::FilterStatus ProxyFilter::onWrite(Buffer::Instance& data, bool) {
  write_buffer_.add(data);

  try {
    decoder_->onData(write_buffer_);
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "memcached decoding error: {}", e.what());
    stats_.decoding_error_.inc();
  }

  return Network::FilterStatus::Continue;
}

}
}
}
}
