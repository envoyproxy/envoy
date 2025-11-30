#include "source/extensions/common/wasm/context.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/filter.h"
#include "envoy/stats/sink.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/common/wasm/plugin.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/common/expr/context.h"

#include "absl/base/casts.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

#include "eval/public/cel_value.h"
#include "eval/public/containers/field_access.h"
#include "eval/public/containers/field_backed_list_impl.h"
#include "eval/public/containers/field_backed_map_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "include/proxy-wasm/pairs_util.h"
#include "openssl/bytestring.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

using proxy_wasm::MetricType;
using proxy_wasm::Word;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

namespace {

// FilterState prefix for CelState values.
constexpr absl::string_view CelStateKeyPrefix = "wasm.";

// Default behavior for Proxy-Wasm 0.2.* ABI is to not support StopIteration as
// a return value from onRequestHeaders() or onResponseHeaders() plugin
// callbacks.
constexpr bool DefaultAllowOnHeadersStopIteration = false;

using HashPolicy = envoy::config::route::v3::RouteAction::HashPolicy;
using CelState = Filters::Common::Expr::CelState;
using CelStatePrototype = Filters::Common::Expr::CelStatePrototype;

Http::RequestTrailerMapPtr buildRequestTrailerMapFromPairs(const Pairs& pairs) {
  auto map = Http::RequestTrailerMapImpl::create();
  for (auto& p : pairs) {
    // Note: because of the lack of a string_view interface for addCopy and
    // the lack of an interface to add an entry with an empty value and return
    // the entry, there is no efficient way to prevent either a double copy
    // of the value or a double lookup of the entry.
    map->addCopy(Http::LowerCaseString(std::string(p.first)), std::string(p.second));
  }
  return map;
}

Http::RequestHeaderMapPtr buildRequestHeaderMapFromPairs(const Pairs& pairs) {
  auto map = Http::RequestHeaderMapImpl::create();
  for (auto& p : pairs) {
    // Note: because of the lack of a string_view interface for addCopy and
    // the lack of an interface to add an entry with an empty value and return
    // the entry, there is no efficient way to prevent either a double copy
    // of the value or a double lookup of the entry.
    map->addCopy(Http::LowerCaseString(std::string(p.first)), std::string(p.second));
  }
  return map;
}

template <typename P> static uint32_t headerSize(const P& p) { return p ? p->size() : 0; }

} // namespace

// Test support.

size_t Buffer::size() const {
  if (const_buffer_instance_) {
    return const_buffer_instance_->length();
  }
  return proxy_wasm::BufferBase::size();
}

WasmResult Buffer::copyTo(WasmBase* wasm, size_t start, size_t length, uint64_t ptr_ptr,
                          uint64_t size_ptr) const {
  if (const_buffer_instance_) {
    uint64_t pointer;
    auto p = wasm->allocMemory(length, &pointer);
    if (!p) {
      return WasmResult::InvalidMemoryAccess;
    }
    const_buffer_instance_->copyOut(start, length, p);
    if (!wasm->wasm_vm()->setWord(ptr_ptr, Word(pointer))) {
      return WasmResult::InvalidMemoryAccess;
    }
    if (!wasm->wasm_vm()->setWord(size_ptr, Word(length))) {
      return WasmResult::InvalidMemoryAccess;
    }
    return WasmResult::Ok;
  }
  return proxy_wasm::BufferBase::copyTo(wasm, start, length, ptr_ptr, size_ptr);
}

WasmResult Buffer::copyFrom(size_t start, size_t length, std::string_view data) {
  if (buffer_instance_) {
    if (start == 0) {
      if (length != 0) {
        buffer_instance_->drain(length);
      }
      buffer_instance_->prepend(toAbslStringView(data));
      return WasmResult::Ok;
    } else if (start >= buffer_instance_->length()) {
      buffer_instance_->add(toAbslStringView(data));
      return WasmResult::Ok;
    } else {
      return WasmResult::BadArgument;
    }
  }
  if (const_buffer_instance_) { // This buffer is immutable.
    return WasmResult::BadArgument;
  }
  return proxy_wasm::BufferBase::copyFrom(start, length, data);
}

Context::Context() = default;
Context::Context(Wasm* wasm) : ContextBase(wasm) {
  if (wasm != nullptr) {
    abi_version_ = wasm->abi_version_;
  }
}
Context::Context(Wasm* wasm, const PluginSharedPtr& plugin) : ContextBase(wasm, plugin) {
  if (wasm != nullptr) {
    abi_version_ = wasm->abi_version_;
  }
  root_local_info_ = &this->plugin()->localInfo();
  allow_on_headers_stop_iteration_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      this->plugin()->wasmConfig().config(), allow_on_headers_stop_iteration,
      DefaultAllowOnHeadersStopIteration);
}
Context::Context(Wasm* wasm, uint32_t root_context_id, PluginHandleSharedPtr plugin_handle)
    : ContextBase(wasm, root_context_id, plugin_handle), plugin_handle_(plugin_handle) {
  if (wasm != nullptr) {
    abi_version_ = wasm->abi_version_;
  }
  allow_on_headers_stop_iteration_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      plugin()->wasmConfig().config(), allow_on_headers_stop_iteration,
      DefaultAllowOnHeadersStopIteration);
}

Wasm* Context::wasm() const { return static_cast<Wasm*>(wasm_); }
Plugin* Context::plugin() const { return static_cast<Plugin*>(plugin_.get()); }
Context* Context::rootContext() const { return static_cast<Context*>(root_context()); }
Upstream::ClusterManager& Context::clusterManager() const { return wasm()->clusterManager(); }

void Context::error(std::string_view message) { ENVOY_LOG(trace, message); }

uint64_t Context::getCurrentTimeNanoseconds() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             wasm()->time_source_.systemTime().time_since_epoch())
      .count();
}

uint64_t Context::getMonotonicTimeNanoseconds() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             wasm()->time_source_.monotonicTime().time_since_epoch())
      .count();
}

void Context::onCloseTCP() {
  if (tcp_connection_closed_ || !in_vm_context_created_) {
    return;
  }
  tcp_connection_closed_ = true;
  onDone();
  onLog();
  onDelete();
}

void Context::onResolveDns(uint32_t token, Envoy::Network::DnsResolver::ResolutionStatus status,
                           std::list<Envoy::Network::DnsResponse>&& response) {
  proxy_wasm::DeferAfterCallActions actions(this);
  if (wasm()->isFailed() || !wasm()->on_resolve_dns_) {
    return;
  }
  if (status != Network::DnsResolver::ResolutionStatus::Completed) {
    buffer_.set("");
    wasm()->on_resolve_dns_(this, id_, token, 0);
    return;
  }
  // buffer format:
  //    4 bytes number of entries = N
  //    N * 4 bytes TTL for each entry
  //    N * null-terminated addresses
  uint32_t s = 4; // length
  for (auto& e : response) {
    s += 4;                                                // for TTL
    s += e.addrInfo().address_->asStringView().size() + 1; // null terminated.
  }
  auto buffer = std::unique_ptr<char[]>(new char[s]);
  char* b = buffer.get();
  uint32_t n = response.size();
  safeMemcpyUnsafeDst(b, &n);
  b += sizeof(uint32_t);
  for (auto& e : response) {
    uint32_t ttl = e.addrInfo().ttl_.count();
    safeMemcpyUnsafeDst(b, &ttl);
    b += sizeof(uint32_t);
  };
  for (auto& e : response) {
    memcpy(b, e.addrInfo().address_->asStringView().data(), // NOLINT(safe-memcpy)
           e.addrInfo().address_->asStringView().size());
    b += e.addrInfo().address_->asStringView().size();
    *b++ = 0;
  };
  buffer_.set(std::move(buffer), s);
  wasm()->on_resolve_dns_(this, id_, token, s);
}

template <typename I> inline uint32_t align(uint32_t i) {
  return (i + sizeof(I) - 1) & ~(sizeof(I) - 1);
}

template <typename I> inline char* align(char* p) {
  return reinterpret_cast<char*>((reinterpret_cast<uintptr_t>(p) + sizeof(I) - 1) &
                                 ~(sizeof(I) - 1));
}

void Context::onStatsUpdate(Envoy::Stats::MetricSnapshot& snapshot) {
  proxy_wasm::DeferAfterCallActions actions(this);
  if (wasm()->isFailed() || !wasm()->on_stats_update_) {
    return;
  }
  // buffer format:
  //  uint32 size of block of this type
  //  uint32 type
  //  uint32 count
  //    uint32 length of name
  //    name
  //    8 byte alignment padding
  //    8 bytes of absolute value
  //    8 bytes of delta  (if appropriate, e.g. for counters)
  //  uint32 size of block of this type

  uint32_t counter_block_size = 3 * sizeof(uint32_t); // type of stat
  uint32_t num_counters = snapshot.counters().size();
  uint32_t counter_type = 1;

  uint32_t gauge_block_size = 3 * sizeof(uint32_t); // type of stat
  uint32_t num_gauges = snapshot.gauges().size();
  uint32_t gauge_type = 2;

  uint32_t n = 0;
  uint64_t v = 0;

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      counter_block_size += sizeof(uint32_t) + counter.counter_.get().name().size();
      counter_block_size = align<uint64_t>(counter_block_size + 2 * sizeof(uint64_t));
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      gauge_block_size += sizeof(uint32_t) + gauge.get().name().size();
      gauge_block_size += align<uint64_t>(gauge_block_size + sizeof(uint64_t));
    }
  }

  auto buffer = std::unique_ptr<char[]>(new char[counter_block_size + gauge_block_size]);
  char* b = buffer.get();

  safeMemcpyUnsafeDst(b, &counter_block_size);
  b += sizeof(uint32_t);
  safeMemcpyUnsafeDst(b, &counter_type);
  b += sizeof(uint32_t);
  safeMemcpyUnsafeDst(b, &num_counters);
  b += sizeof(uint32_t);

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      n = counter.counter_.get().name().size();
      safeMemcpyUnsafeDst(b, &n);
      b += sizeof(uint32_t);
      memcpy(b, counter.counter_.get().name().data(), // NOLINT(safe-memcpy)
             counter.counter_.get().name().size());
      b = align<uint64_t>(b + counter.counter_.get().name().size());
      v = counter.counter_.get().value();
      safeMemcpyUnsafeDst(b, &v);
      b += sizeof(uint64_t);
      v = counter.delta_;
      safeMemcpyUnsafeDst(b, &v);
      b += sizeof(uint64_t);
    }
  }

  safeMemcpyUnsafeDst(b, &gauge_block_size);
  b += sizeof(uint32_t);
  safeMemcpyUnsafeDst(b, &gauge_type);
  b += sizeof(uint32_t);
  safeMemcpyUnsafeDst(b, &num_gauges);
  b += sizeof(uint32_t);

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      n = gauge.get().name().size();
      safeMemcpyUnsafeDst(b, &n);
      b += sizeof(uint32_t);
      memcpy(b, gauge.get().name().data(), gauge.get().name().size()); // NOLINT(safe-memcpy)
      b = align<uint64_t>(b + gauge.get().name().size());
      v = gauge.get().value();
      safeMemcpyUnsafeDst(b, &v);
      b += sizeof(uint64_t);
    }
  }
  buffer_.set(std::move(buffer), counter_block_size + gauge_block_size);
  wasm()->on_stats_update_(this, id_, counter_block_size + gauge_block_size);
}

// Native serializer carrying over bit representation from CEL value to the extension.
// This implementation assumes that the value type is static and known to the consumer.
WasmResult serializeValue(Filters::Common::Expr::CelValue value, std::string* result) {
  using Filters::Common::Expr::CelValue;
  int64_t out_int64;
  uint64_t out_uint64;
  double out_double;
  bool out_bool;
  const Protobuf::Message* out_message;
  switch (value.type()) {
  case CelValue::Type::kString:
    result->assign(value.StringOrDie().value().data(), value.StringOrDie().value().size());
    return WasmResult::Ok;
  case CelValue::Type::kBytes:
    result->assign(value.BytesOrDie().value().data(), value.BytesOrDie().value().size());
    return WasmResult::Ok;
  case CelValue::Type::kInt64:
    out_int64 = value.Int64OrDie();
    result->assign(reinterpret_cast<const char*>(&out_int64), sizeof(int64_t));
    return WasmResult::Ok;
  case CelValue::Type::kUint64:
    out_uint64 = value.Uint64OrDie();
    result->assign(reinterpret_cast<const char*>(&out_uint64), sizeof(uint64_t));
    return WasmResult::Ok;
  case CelValue::Type::kDouble:
    out_double = value.DoubleOrDie();
    result->assign(reinterpret_cast<const char*>(&out_double), sizeof(double));
    return WasmResult::Ok;
  case CelValue::Type::kBool:
    out_bool = value.BoolOrDie();
    result->assign(reinterpret_cast<const char*>(&out_bool), sizeof(bool));
    return WasmResult::Ok;
  case CelValue::Type::kDuration:
    // Warning: loss of precision to nanoseconds
    out_int64 = absl::ToInt64Nanoseconds(value.DurationOrDie());
    result->assign(reinterpret_cast<const char*>(&out_int64), sizeof(int64_t));
    return WasmResult::Ok;
  case CelValue::Type::kTimestamp:
    // Warning: loss of precision to nanoseconds
    out_int64 = absl::ToUnixNanos(value.TimestampOrDie());
    result->assign(reinterpret_cast<const char*>(&out_int64), sizeof(int64_t));
    return WasmResult::Ok;
  case CelValue::Type::kMessage:
    out_message = value.MessageOrDie();
    result->clear();
    if (!out_message || out_message->SerializeToString(result)) {
      return WasmResult::Ok;
    }
    return WasmResult::SerializationFailure;
  case CelValue::Type::kMap: {
    const auto& map = *value.MapOrDie();
    auto keys_list = map.ListKeys();
    if (!keys_list.ok()) {
      return WasmResult::SerializationFailure;
    }
    const auto& keys = *keys_list.value();
    std::vector<std::pair<std::string, std::string>> pairs(map.size(), std::make_pair("", ""));
    for (auto i = 0; i < map.size(); i++) {
      if (serializeValue(keys[i], &pairs[i].first) != WasmResult::Ok) {
        return WasmResult::SerializationFailure;
      }
      if (serializeValue(map[keys[i]].value(), &pairs[i].second) != WasmResult::Ok) {
        return WasmResult::SerializationFailure;
      }
    }
    auto size = proxy_wasm::PairsUtil::pairsSize(pairs);
    // prevent string inlining which violates byte alignment
    result->resize(std::max(size, static_cast<size_t>(30)));
    if (!proxy_wasm::PairsUtil::marshalPairs(pairs, result->data(), size)) {
      return WasmResult::SerializationFailure;
    }
    result->resize(size);
    return WasmResult::Ok;
  }
  case CelValue::Type::kList: {
    const auto& list = *value.ListOrDie();
    std::vector<std::pair<std::string, std::string>> pairs(list.size(), std::make_pair("", ""));
    for (auto i = 0; i < list.size(); i++) {
      if (serializeValue(list[i], &pairs[i].first) != WasmResult::Ok) {
        return WasmResult::SerializationFailure;
      }
    }
    auto size = proxy_wasm::PairsUtil::pairsSize(pairs);
    // prevent string inlining which violates byte alignment
    if (size < 30) {
      result->reserve(30);
    }
    result->resize(size);
    if (!proxy_wasm::PairsUtil::marshalPairs(pairs, result->data(), size)) {
      return WasmResult::SerializationFailure;
    }
    return WasmResult::Ok;
  }
  default:
    break;
  }
  return WasmResult::SerializationFailure;
}

#define PROPERTY_TOKENS(_f) _f(PLUGIN_NAME) _f(PLUGIN_ROOT_ID) _f(PLUGIN_VM_ID) _f(CONNECTION_ID)

static inline std::string downCase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

#define _DECLARE(_t) _t,
enum class PropertyToken { PROPERTY_TOKENS(_DECLARE) };
#undef _DECLARE

#define _PAIR(_t) {downCase(#_t), PropertyToken::_t},
static absl::flat_hash_map<std::string, PropertyToken> property_tokens = {PROPERTY_TOKENS(_PAIR)};
#undef _PAIR

absl::optional<google::api::expr::runtime::CelValue>
Context::FindValue(absl::string_view name, Protobuf::Arena* arena) const {
  return findValue(name, arena, false);
}

absl::optional<google::api::expr::runtime::CelValue>
Context::findValue(absl::string_view name, Protobuf::Arena* arena, bool last) const {
  using google::api::expr::runtime::CelProtoWrapper;
  using google::api::expr::runtime::CelValue;

  const StreamInfo::StreamInfo* info = getConstRequestStreamInfo();
  // In order to delegate to the StreamActivation method, we have to set the
  // context properties to match the Wasm context properties in all callbacks
  // (e.g. onLog or onEncodeHeaders) for the duration of the call.
  if (root_local_info_) {
    local_info_ = root_local_info_;
  } else if (plugin_) {
    local_info_ = &plugin()->localInfo();
  }
  activation_info_ = info;
  activation_request_headers_ = request_headers_ ? request_headers_ : access_log_request_headers_;
  activation_response_headers_ =
      response_headers_ ? response_headers_ : access_log_response_headers_;
  activation_response_trailers_ =
      response_trailers_ ? response_trailers_ : access_log_response_trailers_;
  auto value = StreamActivation::FindValue(name, arena);
  resetActivation();
  if (value) {
    return value;
  }

  // Convert into a dense token to enable a jump table implementation.
  auto part_token = property_tokens.find(name);
  if (part_token == property_tokens.end()) {
    if (info) {
      std::string key = absl::StrCat(CelStateKeyPrefix, name);
      const CelState* state = info->filterState().getDataReadOnly<CelState>(key);
      if (state == nullptr) {
        if (info->upstreamInfo().has_value() &&
            info->upstreamInfo().value().get().upstreamFilterState() != nullptr) {
          state =
              info->upstreamInfo().value().get().upstreamFilterState()->getDataReadOnly<CelState>(
                  key);
        }
      }
      if (state != nullptr) {
        return state->exprValue(arena, last);
      }
    }
    return {};
  }

  switch (part_token->second) {
  case PropertyToken::CONNECTION_ID: {
    auto conn = getConnection();
    if (conn) {
      return CelValue::CreateUint64(conn->id());
    }
    break;
  }
  case PropertyToken::PLUGIN_NAME:
    if (plugin_) {
      return CelValue::CreateStringView(plugin()->name_);
    }
    break;
  case PropertyToken::PLUGIN_ROOT_ID:
    return CelValue::CreateStringView(toAbslStringView(root_id()));
  case PropertyToken::PLUGIN_VM_ID:
    return CelValue::CreateStringView(toAbslStringView(wasm()->vm_id()));
  }
  return {};
}

WasmResult Context::getProperty(std::string_view path, std::string* result) {
  using google::api::expr::runtime::CelValue;

  bool first = true;
  CelValue value;
  Protobuf::Arena arena;

  size_t start = 0;
  while (true) {
    if (start >= path.size()) {
      break;
    }

    size_t end = path.find('\0', start);
    if (end == absl::string_view::npos) {
      end = start + path.size();
    }
    auto part = path.substr(start, end - start);
    start = end + 1;

    if (first) {
      // top-level identifier
      first = false;
      auto top_value = findValue(toAbslStringView(part), &arena, start >= path.size());
      if (!top_value.has_value()) {
        return WasmResult::NotFound;
      }
      value = top_value.value();
    } else if (value.IsMap()) {
      auto& map = *value.MapOrDie();
      auto field = map[CelValue::CreateStringView(toAbslStringView(part))];
      if (!field.has_value()) {
        return WasmResult::NotFound;
      }
      value = field.value();
    } else if (value.IsMessage()) {
      auto msg = value.MessageOrDie();
      if (msg == nullptr) {
        return WasmResult::NotFound;
      }
      const Protobuf::Descriptor* desc = msg->GetDescriptor();
      const Protobuf::FieldDescriptor* field_desc = desc->FindFieldByName(std::string(part));
      if (field_desc == nullptr) {
        return WasmResult::NotFound;
      }
      if (field_desc->is_map()) {
        value = CelValue::CreateMap(
            Protobuf::Arena::Create<google::api::expr::runtime::FieldBackedMapImpl>(
                &arena, msg, field_desc, &arena));
      } else if (field_desc->is_repeated()) {
        value = CelValue::CreateList(
            Protobuf::Arena::Create<google::api::expr::runtime::FieldBackedListImpl>(
                &arena, msg, field_desc, &arena));
      } else {
        auto status =
            google::api::expr::runtime::CreateValueFromSingleField(msg, field_desc, &arena, &value);
        if (!status.ok()) {
          return WasmResult::InternalFailure;
        }
      }
    } else if (value.IsList()) {
      auto& list = *value.ListOrDie();
      int idx = 0;
      if (!absl::SimpleAtoi(toAbslStringView(part), &idx)) {
        return WasmResult::NotFound;
      }
      if (idx < 0 || idx >= list.size()) {
        return WasmResult::NotFound;
      }
      value = list[idx];
    } else {
      return WasmResult::NotFound;
    }
  }

  return serializeValue(value, result);
}

// Header/Trailer/Metadata Maps.
Http::HeaderMap* Context::getMap(WasmHeaderMapType type) {
  switch (type) {
  case WasmHeaderMapType::RequestHeaders:
    return request_headers_;
  case WasmHeaderMapType::RequestTrailers:
    if (request_trailers_ == nullptr && request_body_buffer_ && end_of_stream_ &&
        decoder_callbacks_) {
      request_trailers_ = &decoder_callbacks_->addDecodedTrailers();
    }
    return request_trailers_;
  case WasmHeaderMapType::ResponseHeaders:
    return response_headers_;
  case WasmHeaderMapType::ResponseTrailers:
    if (response_trailers_ == nullptr && response_body_buffer_ && end_of_stream_ &&
        encoder_callbacks_) {
      response_trailers_ = &encoder_callbacks_->addEncodedTrailers();
    }
    return response_trailers_;
  default:
    return nullptr;
  }
}

const Http::HeaderMap* Context::getConstMap(WasmHeaderMapType type) {
  switch (type) {
  case WasmHeaderMapType::RequestHeaders:
    if (access_log_phase_) {
      return access_log_request_headers_;
    }
    return request_headers_;
  case WasmHeaderMapType::RequestTrailers:
    if (access_log_phase_) {
      return nullptr;
    }
    return request_trailers_;
  case WasmHeaderMapType::ResponseHeaders:
    if (access_log_phase_) {
      return access_log_response_headers_;
    }
    return response_headers_;
  case WasmHeaderMapType::ResponseTrailers:
    if (access_log_phase_) {
      return access_log_response_trailers_;
    }
    return response_trailers_;
  case WasmHeaderMapType::GrpcReceiveInitialMetadata:
    return rootContext()->grpc_receive_initial_metadata_.get();
  case WasmHeaderMapType::GrpcReceiveTrailingMetadata:
    return rootContext()->grpc_receive_trailing_metadata_.get();
  case WasmHeaderMapType::HttpCallResponseHeaders: {
    Envoy::Http::ResponseMessagePtr* response = rootContext()->http_call_response_;
    if (response) {
      return &(*response)->headers();
    }
    return nullptr;
  }
  case WasmHeaderMapType::HttpCallResponseTrailers: {
    Envoy::Http::ResponseMessagePtr* response = rootContext()->http_call_response_;
    if (response) {
      return (*response)->trailers();
    }
    return nullptr;
  }
  }
  IS_ENVOY_BUG("unexpected");
  return nullptr;
}

WasmResult Context::addHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                                      std::string_view value) {
  auto map = getMap(type);
  if (!map) {
    return WasmResult::BadArgument;
  }
  const Http::LowerCaseString lower_key{std::string(key)};
  map->addCopy(lower_key, std::string(value));
  onHeadersModified(type);
  return WasmResult::Ok;
}

WasmResult Context::getHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                                      std::string_view* value) {
  auto map = getConstMap(type);
  if (!map) {
    if (access_log_phase_) {
      // Maps might point to nullptr in the access log phase.
      if (wasm()->abiVersion() == proxy_wasm::AbiVersion::ProxyWasm_0_1_0) {
        *value = "";
        return WasmResult::Ok;
      } else {
        return WasmResult::NotFound;
      }
    }
    // Requested map type is not currently available.
    return WasmResult::BadArgument;
  }
  const Http::LowerCaseString lower_key{std::string(key)};
  const auto entry = map->get(lower_key);
  if (entry.empty()) {
    if (wasm()->abiVersion() == proxy_wasm::AbiVersion::ProxyWasm_0_1_0) {
      *value = "";
      return WasmResult::Ok;
    } else {
      return WasmResult::NotFound;
    }
  }
  // TODO(kyessenov, PiotrSikora): This needs to either return a concatenated list of values, or
  // the ABI needs to be changed to return multiple values. This is a potential security issue.
  *value = toStdStringView(entry[0]->value().getStringView());
  return WasmResult::Ok;
}

Pairs headerMapToPairs(const Http::HeaderMap* map) {
  if (!map) {
    return {};
  }
  Pairs pairs;
  pairs.reserve(map->size());
  map->iterate([&pairs](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    pairs.push_back(std::make_pair(toStdStringView(header.key().getStringView()),
                                   toStdStringView(header.value().getStringView())));
    return Http::HeaderMap::Iterate::Continue;
  });
  return pairs;
}

WasmResult Context::getHeaderMapPairs(WasmHeaderMapType type, Pairs* result) {
  *result = headerMapToPairs(getConstMap(type));
  return WasmResult::Ok;
}

WasmResult Context::setHeaderMapPairs(WasmHeaderMapType type, const Pairs& pairs) {
  auto map = getMap(type);
  if (!map) {
    return WasmResult::BadArgument;
  }
  std::vector<std::string> keys;
  map->iterate([&keys](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    keys.push_back(std::string(header.key().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
  for (auto& k : keys) {
    const Http::LowerCaseString lower_key{k};
    map->remove(lower_key);
  }
  for (auto& p : pairs) {
    const Http::LowerCaseString lower_key{std::string(p.first)};
    map->addCopy(lower_key, std::string(p.second));
  }
  onHeadersModified(type);
  return WasmResult::Ok;
}

WasmResult Context::removeHeaderMapValue(WasmHeaderMapType type, std::string_view key) {
  auto map = getMap(type);
  if (!map) {
    return WasmResult::BadArgument;
  }
  const Http::LowerCaseString lower_key{std::string(key)};
  map->remove(lower_key);
  onHeadersModified(type);
  return WasmResult::Ok;
}

WasmResult Context::replaceHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                                          std::string_view value) {
  auto map = getMap(type);
  if (!map) {
    return WasmResult::BadArgument;
  }
  const Http::LowerCaseString lower_key{std::string(key)};
  map->setCopy(lower_key, toAbslStringView(value));
  onHeadersModified(type);
  return WasmResult::Ok;
}

WasmResult Context::getHeaderMapSize(WasmHeaderMapType type, uint32_t* result) {
  auto map = getMap(type);
  if (!map) {
    return WasmResult::BadArgument;
  }
  *result = map->byteSize();
  return WasmResult::Ok;
}

// Buffer

BufferInterface* Context::getBuffer(WasmBufferType type) {
  Envoy::Http::ResponseMessagePtr* response = nullptr;
  switch (type) {
  case WasmBufferType::CallData:
    // Set before the call.
    return &buffer_;
  case WasmBufferType::VmConfiguration:
    return buffer_.set(wasm()->vm_configuration());
  case WasmBufferType::PluginConfiguration:
    if (temp_plugin_) {
      return buffer_.set(temp_plugin_->plugin_configuration_);
    }
    return nullptr;
  case WasmBufferType::HttpRequestBody:
    if (buffering_request_body_ && decoder_callbacks_) {
      // We need the mutable version, so capture it using a callback.
      // TODO: consider adding a mutableDecodingBuffer() interface.
      ::Envoy::Buffer::Instance* buffer_instance{};
      decoder_callbacks_->modifyDecodingBuffer(
          [&buffer_instance](::Envoy::Buffer::Instance& buffer) { buffer_instance = &buffer; });
      return buffer_.set(buffer_instance);
    }
    return buffer_.set(request_body_buffer_);
  case WasmBufferType::HttpResponseBody:
    if (buffering_response_body_ && encoder_callbacks_) {
      // TODO: consider adding a mutableDecodingBuffer() interface.
      ::Envoy::Buffer::Instance* buffer_instance{};
      encoder_callbacks_->modifyEncodingBuffer(
          [&buffer_instance](::Envoy::Buffer::Instance& buffer) { buffer_instance = &buffer; });
      return buffer_.set(buffer_instance);
    }
    return buffer_.set(response_body_buffer_);
  case WasmBufferType::NetworkDownstreamData:
    return buffer_.set(network_downstream_data_buffer_);
  case WasmBufferType::NetworkUpstreamData:
    return buffer_.set(network_upstream_data_buffer_);
  case WasmBufferType::HttpCallResponseBody:
    response = rootContext()->http_call_response_;
    if (response) {
      auto& body = (*response)->body();
      return buffer_.set(
          std::string_view(static_cast<const char*>(body.linearize(body.length())), body.length()));
    }
    return nullptr;
  case WasmBufferType::GrpcReceiveBuffer:
    return buffer_.set(rootContext()->grpc_receive_buffer_.get());
  default:
    return nullptr;
  }
}

void Context::onDownstreamConnectionClose(CloseType close_type) {
  ContextBase::onDownstreamConnectionClose(close_type);
  downstream_closed_ = true;
  onCloseTCP();
}

void Context::onUpstreamConnectionClose(CloseType close_type) {
  ContextBase::onUpstreamConnectionClose(close_type);
  upstream_closed_ = true;
  if (downstream_closed_) {
    onCloseTCP();
  }
}

// Async call via HTTP
WasmResult Context::httpCall(std::string_view cluster, const Pairs& request_headers,
                             std::string_view request_body, const Pairs& request_trailers,
                             int timeout_milliseconds, uint32_t* token_ptr) {
  if (timeout_milliseconds < 0) {
    return WasmResult::BadArgument;
  }
  auto cluster_string = std::string(cluster);
  const auto thread_local_cluster = clusterManager().getThreadLocalCluster(cluster_string);
  if (thread_local_cluster == nullptr) {
    return WasmResult::BadArgument;
  }

  Http::RequestMessagePtr message(
      new Http::RequestMessageImpl(buildRequestHeaderMapFromPairs(request_headers)));

  // Check that we were provided certain headers.
  if (message->headers().Path() == nullptr || message->headers().Method() == nullptr ||
      message->headers().Host() == nullptr) {
    return WasmResult::BadArgument;
  }

  if (!request_body.empty()) {
    message->body().add(toAbslStringView(request_body));
    message->headers().setContentLength(request_body.size());
  }

  if (!request_trailers.empty()) {
    message->trailers(buildRequestTrailerMapFromPairs(request_trailers));
  }

  absl::optional<std::chrono::milliseconds> timeout;
  if (timeout_milliseconds > 0) {
    timeout = std::chrono::milliseconds(timeout_milliseconds);
  }

  uint32_t token = wasm()->nextHttpCallId();
  auto& handler = http_request_[token];
  handler.context_ = this;
  handler.token_ = token;

  // set default hash policy to be based on :authority to enable consistent hash
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(timeout);
  Protobuf::RepeatedPtrField<HashPolicy> hash_policy;
  hash_policy.Add()->mutable_header()->set_header_name(Http::Headers::get().Host.get());
  options.setHashPolicy(hash_policy);
  options.setSendXff(false);
  auto http_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), handler, options);
  if (!http_request) {
    http_request_.erase(token);
    return WasmResult::InternalFailure;
  }
  handler.request_ = http_request;
  *token_ptr = token;
  return WasmResult::Ok;
}

WasmResult Context::grpcCall(std::string_view grpc_service, std::string_view service_name,
                             std::string_view method_name, const Pairs& initial_metadata,
                             std::string_view request, std::chrono::milliseconds timeout,
                             uint32_t* token_ptr) {
  GrpcService service_proto;
  if (!service_proto.ParseFromString(grpc_service)) {
    auto cluster_name = std::string(grpc_service.substr(0, grpc_service.size()));
    const auto thread_local_cluster = clusterManager().getThreadLocalCluster(cluster_name);
    if (thread_local_cluster == nullptr) {
      // TODO(shikugawa): The reason to keep return status as `BadArgument` is not to force
      // callers to change their own codebase with ABI 0.1.x. We should treat this failure as
      // `BadArgument` after ABI 0.2.x will have released.
      return WasmResult::ParseFailure;
    }
    service_proto.mutable_envoy_grpc()->set_cluster_name(cluster_name);
  }
  uint32_t token = wasm()->nextGrpcCallId();
  auto& handler = grpc_call_request_[token];
  handler.context_ = this;
  handler.token_ = token;
  auto client_or_error = clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
      service_proto, *wasm()->scope_, true /* skip_cluster_check */);
  if (!client_or_error.status().ok()) {
    return WasmResult::BadArgument;
  }
  auto grpc_client = client_or_error.value();
  grpc_initial_metadata_ = buildRequestHeaderMapFromPairs(initial_metadata);

  // set default hash policy to be based on :authority to enable consistent hash
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(timeout);
  Protobuf::RepeatedPtrField<HashPolicy> hash_policy;
  hash_policy.Add()->mutable_header()->set_header_name(Http::Headers::get().Host.get());
  options.setHashPolicy(hash_policy);
  options.setSendXff(false);

  auto grpc_request =
      grpc_client->sendRaw(toAbslStringView(service_name), toAbslStringView(method_name),
                           std::make_unique<::Envoy::Buffer::OwnedImpl>(toAbslStringView(request)),
                           handler, Tracing::NullSpan::instance(), options);
  if (!grpc_request) {
    grpc_call_request_.erase(token);
    return WasmResult::InternalFailure;
  }
  handler.client_ = std::move(grpc_client);
  handler.request_ = grpc_request;
  *token_ptr = token;
  return WasmResult::Ok;
}

WasmResult Context::grpcStream(std::string_view grpc_service, std::string_view service_name,
                               std::string_view method_name, const Pairs& initial_metadata,
                               uint32_t* token_ptr) {
  GrpcService service_proto;
  if (!service_proto.ParseFromString(grpc_service)) {
    auto cluster_name = std::string(grpc_service.substr(0, grpc_service.size()));
    const auto thread_local_cluster = clusterManager().getThreadLocalCluster(cluster_name);
    if (thread_local_cluster == nullptr) {
      // TODO(shikugawa): The reason to keep return status as `BadArgument` is not to force
      // callers to change their own codebase with ABI 0.1.x. We should treat this failure as
      // `BadArgument` after ABI 0.2.x will have released.
      return WasmResult::ParseFailure;
    }
    service_proto.mutable_envoy_grpc()->set_cluster_name(cluster_name);
  }
  uint32_t token = wasm()->nextGrpcStreamId();
  auto& handler = grpc_stream_[token];
  handler.context_ = this;
  handler.token_ = token;
  auto client_or_error = clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
      service_proto, *wasm()->scope_, true /* skip_cluster_check */);
  if (!client_or_error.status().ok()) {
    return WasmResult::BadArgument;
  }
  auto grpc_client = client_or_error.value();
  grpc_initial_metadata_ = buildRequestHeaderMapFromPairs(initial_metadata);

  // set default hash policy to be based on :authority to enable consistent hash
  Http::AsyncClient::StreamOptions options;
  Protobuf::RepeatedPtrField<HashPolicy> hash_policy;
  hash_policy.Add()->mutable_header()->set_header_name(Http::Headers::get().Host.get());
  options.setHashPolicy(hash_policy);
  options.setSendXff(false);

  auto grpc_stream = grpc_client->startRaw(toAbslStringView(service_name),
                                           toAbslStringView(method_name), handler, options);
  if (!grpc_stream) {
    grpc_stream_.erase(token);
    return WasmResult::InternalFailure;
  }
  handler.client_ = std::move(grpc_client);
  handler.stream_ = grpc_stream;
  *token_ptr = token;
  return WasmResult::Ok;
}

// NB: this is currently called inline, so the token is known to be that of the currently
// executing grpcCall or grpcStream.
void Context::onGrpcCreateInitialMetadata(uint32_t /* token */,
                                          Http::RequestHeaderMap& initial_metadata) {
  if (grpc_initial_metadata_) {
    Http::HeaderMapImpl::copyFrom(initial_metadata, *grpc_initial_metadata_);
    grpc_initial_metadata_.reset();
  }
}

// StreamInfo
const StreamInfo::StreamInfo* Context::getConstRequestStreamInfo() const {
  if (encoder_callbacks_) {
    return &encoder_callbacks_->streamInfo();
  } else if (decoder_callbacks_) {
    return &decoder_callbacks_->streamInfo();
  } else if (access_log_stream_info_) {
    return access_log_stream_info_;
  } else if (network_read_filter_callbacks_) {
    return &network_read_filter_callbacks_->connection().streamInfo();
  } else if (network_write_filter_callbacks_) {
    return &network_write_filter_callbacks_->connection().streamInfo();
  }
  return nullptr;
}

StreamInfo::StreamInfo* Context::getRequestStreamInfo() const {
  if (encoder_callbacks_) {
    return &encoder_callbacks_->streamInfo();
  } else if (decoder_callbacks_) {
    return &decoder_callbacks_->streamInfo();
  } else if (network_read_filter_callbacks_) {
    return &network_read_filter_callbacks_->connection().streamInfo();
  } else if (network_write_filter_callbacks_) {
    return &network_write_filter_callbacks_->connection().streamInfo();
  }
  return nullptr;
}

const Network::Connection* Context::getConnection() const {
  if (encoder_callbacks_) {
    return encoder_callbacks_->connection().ptr();
  } else if (decoder_callbacks_) {
    return decoder_callbacks_->connection().ptr();
  } else if (network_read_filter_callbacks_) {
    return &network_read_filter_callbacks_->connection();
  } else if (network_write_filter_callbacks_) {
    return &network_write_filter_callbacks_->connection();
  }
  return nullptr;
}

WasmResult Context::setProperty(std::string_view path, std::string_view value) {
  auto* stream_info = getRequestStreamInfo();
  if (!stream_info) {
    return WasmResult::NotFound;
  }
  std::string key;
  absl::StrAppend(&key, CelStateKeyPrefix, toAbslStringView(path));
  CelState* state = stream_info->filterState()->getDataMutable<CelState>(key);
  if (state == nullptr) {
    const auto& it = rootContext()->state_prototypes_.find(toAbslStringView(path));
    const CelStatePrototype& prototype =
        it == rootContext()->state_prototypes_.end()
            ? Filters::Common::Expr::DefaultCelStatePrototype::get()
            : *it->second.get(); // NOLINT
    auto state_ptr = std::make_unique<CelState>(prototype);
    state = state_ptr.get();
    stream_info->filterState()->setData(key, std::move(state_ptr),
                                        StreamInfo::FilterState::StateType::Mutable,
                                        prototype.life_span_);
  }
  if (!state->setValue(toAbslStringView(value))) {
    return WasmResult::BadArgument;
  }
  return WasmResult::Ok;
}

WasmResult Context::setEnvoyFilterState(std::string_view path, std::string_view value,
                                        StreamInfo::FilterState::LifeSpan life_span) {
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(path);
  if (!factory) {
    return WasmResult::NotFound;
  }

  auto object = factory->createFromBytes(value);
  if (!object) {
    return WasmResult::BadArgument;
  }

  auto* stream_info = getRequestStreamInfo();
  if (!stream_info) {
    return WasmResult::NotFound;
  }

  stream_info->filterState()->setData(path, std::move(object),
                                      StreamInfo::FilterState::StateType::Mutable, life_span);
  return WasmResult::Ok;
}

WasmResult
Context::declareProperty(std::string_view path,
                         Filters::Common::Expr::CelStatePrototypeConstPtr state_prototype) {
  // Do not delete existing schema since it can be referenced by state objects.
  if (state_prototypes_.find(toAbslStringView(path)) == state_prototypes_.end()) {
    state_prototypes_[toAbslStringView(path)] = std::move(state_prototype);
    return WasmResult::Ok;
  }
  return WasmResult::BadArgument;
}

WasmResult Context::log(uint32_t level, std::string_view message) {
  switch (static_cast<spdlog::level::level_enum>(level)) {
  case spdlog::level::trace:
    ENVOY_LOG(trace, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::debug:
    ENVOY_LOG(debug, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::info:
    ENVOY_LOG(info, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::warn:
    ENVOY_LOG(warn, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::err:
    ENVOY_LOG(error, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::critical:
    ENVOY_LOG(critical, "wasm log{}: {}", log_prefix(), message);
    return WasmResult::Ok;
  case spdlog::level::off:
    PANIC("not implemented");
  case spdlog::level::n_levels:
    PANIC("not implemented");
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

uint32_t Context::getLogLevel() {
  // Like the "log" call above, assume that spdlog level as an int
  // matches the enum in the SDK
  return static_cast<uint32_t>(ENVOY_LOGGER().level());
}

//
// Calls into the Wasm code.
//
bool Context::validateConfiguration(std::string_view configuration,
                                    const std::shared_ptr<PluginBase>& plugin_base) {
  auto plugin = std::static_pointer_cast<Plugin>(plugin_base);
  if (!wasm()->validate_configuration_) {
    return true;
  }
  temp_plugin_ = plugin_base;
  auto result =
      wasm()
          ->validate_configuration_(this, id_, static_cast<uint32_t>(configuration.size()))
          .u64_ != 0;
  temp_plugin_.reset();
  return result;
}

std::string_view Context::getConfiguration() {
  if (temp_plugin_) {
    return temp_plugin_->plugin_configuration_;
  } else {
    return wasm()->vm_configuration();
  }
};

std::pair<uint32_t, std::string_view> Context::getStatus() {
  return std::make_pair(status_code_, toStdStringView(status_message_));
}

void Context::onGrpcReceiveInitialMetadataWrapper(uint32_t token, Http::HeaderMapPtr&& metadata) {
  grpc_receive_initial_metadata_ = std::move(metadata);
  onGrpcReceiveInitialMetadata(token, headerSize(grpc_receive_initial_metadata_));
  grpc_receive_initial_metadata_ = nullptr;
}

void Context::onGrpcReceiveTrailingMetadataWrapper(uint32_t token, Http::HeaderMapPtr&& metadata) {
  grpc_receive_trailing_metadata_ = std::move(metadata);
  onGrpcReceiveTrailingMetadata(token, headerSize(grpc_receive_trailing_metadata_));
  grpc_receive_trailing_metadata_ = nullptr;
}

WasmResult Context::defineMetric(uint32_t metric_type, std::string_view name,
                                 uint32_t* metric_id_ptr) {
  if (metric_type > static_cast<uint32_t>(MetricType::Max)) {
    return WasmResult::BadArgument;
  }
  auto type = static_cast<MetricType>(metric_type);
  // TODO: Consider rethinking the scoping policy as it does not help in this case.
  Stats::StatNameManagedStorage storage(toAbslStringView(name), wasm()->scope_->symbolTable());
  Stats::StatName stat_name = storage.statName();
  // We prefix the given name with custom_stat_name_ so that these user-defined
  // custom metrics can be distinguished from native Envoy metrics.
  if (type == MetricType::Counter) {
    auto id = wasm()->nextCounterMetricId();
    Stats::Counter* c = &Stats::Utility::counterFromElements(
        *wasm()->scope_, {wasm()->custom_stat_namespace_, stat_name});
    wasm()->counters_.emplace(id, c);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  if (type == MetricType::Gauge) {
    auto id = wasm()->nextGaugeMetricId();
    Stats::Gauge* g = &Stats::Utility::gaugeFromStatNames(
        *wasm()->scope_, {wasm()->custom_stat_namespace_, stat_name},
        Stats::Gauge::ImportMode::Accumulate);
    wasm()->gauges_.emplace(id, g);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  // (type == MetricType::Histogram) {
  auto id = wasm()->nextHistogramMetricId();
  Stats::Histogram* h = &Stats::Utility::histogramFromStatNames(
      *wasm()->scope_, {wasm()->custom_stat_namespace_, stat_name},
      Stats::Histogram::Unit::Unspecified);
  wasm()->histograms_.emplace(id, h);
  *metric_id_ptr = id;
  return WasmResult::Ok;
}

WasmResult Context::incrementMetric(uint32_t metric_id, int64_t offset) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      if (offset > 0) {
        it->second->add(offset);
        return WasmResult::Ok;
      } else {
        return WasmResult::BadArgument;
      }
    }
    return WasmResult::NotFound;
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
      if (offset > 0) {
        it->second->add(offset);
        return WasmResult::Ok;
      } else {
        it->second->sub(-offset);
        return WasmResult::Ok;
      }
    }
    return WasmResult::NotFound;
  }
  return WasmResult::BadArgument;
}

WasmResult Context::recordMetric(uint32_t metric_id, uint64_t value) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      it->second->add(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
      it->second->set(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Histogram) {
    auto it = wasm()->histograms_.find(metric_id);
    if (it != wasm()->histograms_.end()) {
      it->second->recordValue(value);
      return WasmResult::Ok;
    }
  }
  return WasmResult::NotFound;
}

WasmResult Context::getMetric(uint32_t metric_id, uint64_t* result_uint64_ptr) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  }
  return WasmResult::BadArgument;
}

Context::~Context() {
  // Cancel any outstanding requests.
  for (auto& p : http_request_) {
    if (p.second.request_ != nullptr) {
      p.second.request_->cancel();
    }
  }
  for (auto& p : grpc_call_request_) {
    if (p.second.request_ != nullptr) {
      p.second.request_->cancel();
    }
  }
  for (auto& p : grpc_stream_) {
    if (p.second.stream_ != nullptr) {
      p.second.stream_->resetStream();
    }
  }
}

Network::FilterStatus convertNetworkFilterStatus(proxy_wasm::FilterStatus status) {
  switch (status) {
  default:
  case proxy_wasm::FilterStatus::Continue:
    return Network::FilterStatus::Continue;
  case proxy_wasm::FilterStatus::StopIteration:
    return Network::FilterStatus::StopIteration;
  }
};

Http::FilterHeadersStatus convertFilterHeadersStatus(proxy_wasm::FilterHeadersStatus status) {
  switch (status) {
  default:
  case proxy_wasm::FilterHeadersStatus::Continue:
    return Http::FilterHeadersStatus::Continue;
  case proxy_wasm::FilterHeadersStatus::StopIteration:
    return Http::FilterHeadersStatus::StopIteration;
  case proxy_wasm::FilterHeadersStatus::StopAllIterationAndBuffer:
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  case proxy_wasm::FilterHeadersStatus::StopAllIterationAndWatermark:
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }
};

Http::FilterTrailersStatus convertFilterTrailersStatus(proxy_wasm::FilterTrailersStatus status) {
  switch (status) {
  default:
  case proxy_wasm::FilterTrailersStatus::Continue:
    return Http::FilterTrailersStatus::Continue;
  case proxy_wasm::FilterTrailersStatus::StopIteration:
    return Http::FilterTrailersStatus::StopIteration;
  }
};

Http::FilterMetadataStatus convertFilterMetadataStatus(proxy_wasm::FilterMetadataStatus status) {
  switch (status) {
  default:
  case proxy_wasm::FilterMetadataStatus::Continue:
    return Http::FilterMetadataStatus::Continue;
  }
};

Http::FilterDataStatus convertFilterDataStatus(proxy_wasm::FilterDataStatus status) {
  switch (status) {
  default:
  case proxy_wasm::FilterDataStatus::Continue:
    return Http::FilterDataStatus::Continue;
  case proxy_wasm::FilterDataStatus::StopIterationAndBuffer:
    return Http::FilterDataStatus::StopIterationAndBuffer;
  case proxy_wasm::FilterDataStatus::StopIterationAndWatermark:
    return Http::FilterDataStatus::StopIterationAndWatermark;
  case proxy_wasm::FilterDataStatus::StopIterationNoBuffer:
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
};

Network::FilterStatus Context::onNewConnection() {
  onCreate();
  return convertNetworkFilterStatus(onNetworkNewConnection());
};

Network::FilterStatus Context::onData(::Envoy::Buffer::Instance& data, bool end_stream) {
  if (!in_vm_context_created_) {
    return Network::FilterStatus::Continue;
  }
  network_downstream_data_buffer_ = &data;
  end_of_stream_ = end_stream;
  auto result = convertNetworkFilterStatus(onDownstreamData(data.length(), end_stream));
  if (result == Network::FilterStatus::Continue) {
    network_downstream_data_buffer_ = nullptr;
  }
  return result;
}

Network::FilterStatus Context::onWrite(::Envoy::Buffer::Instance& data, bool end_stream) {
  if (!in_vm_context_created_) {
    return Network::FilterStatus::Continue;
  }
  network_upstream_data_buffer_ = &data;
  end_of_stream_ = end_stream;
  auto result = convertNetworkFilterStatus(onUpstreamData(data.length(), end_stream));
  if (result == Network::FilterStatus::Continue) {
    network_upstream_data_buffer_ = nullptr;
  }
  if (end_stream) {
    // This is called when seeing end_stream=true and not on an upstream connection event,
    // because registering for latter requires replicating the whole TCP proxy extension.
    onUpstreamConnectionClose(CloseType::Unknown);
  }
  return result;
}

void Context::onEvent(Network::ConnectionEvent event) {
  if (!in_vm_context_created_) {
    return;
  }
  switch (event) {
  case Network::ConnectionEvent::LocalClose:
    onDownstreamConnectionClose(CloseType::Local);
    break;
  case Network::ConnectionEvent::RemoteClose:
    onDownstreamConnectionClose(CloseType::Remote);
    break;
  default:
    break;
  }
}

void Context::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  network_read_filter_callbacks_ = &callbacks;
  network_read_filter_callbacks_->connection().addConnectionCallbacks(*this);
}

void Context::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  network_write_filter_callbacks_ = &callbacks;
}

void Context::log(const Formatter::Context& log_context,
                  const StreamInfo::StreamInfo& stream_info) {
  // `log` may be called multiple times due to mid-request logging -- we only want to run on the
  // last call.
  if (!stream_info.requestComplete().has_value()) {
    return;
  }
  if (!in_vm_context_created_) {
    // If the request is invalid then onRequestHeaders() will not be called and neither will
    // onCreate() in cases like sendLocalReply who short-circuits envoy
    // lifecycle. This is because Envoy does not have a well defined lifetime for the combined
    // HTTP
    // + AccessLog filter. Thus, to log these scenarios, we call onCreate() in log function below.
    onCreate();
  }

  access_log_phase_ = true;
  access_log_request_headers_ = log_context.requestHeaders().ptr();
  // ? request_trailers  ?
  access_log_response_headers_ = log_context.responseHeaders().ptr();
  access_log_response_trailers_ = log_context.responseTrailers().ptr();
  access_log_stream_info_ = &stream_info;

  onLog();

  access_log_phase_ = false;
  access_log_request_headers_ = nullptr;
  // ? request_trailers  ?
  access_log_response_headers_ = nullptr;
  access_log_response_trailers_ = nullptr;
  access_log_stream_info_ = nullptr;
}

void Context::onDestroy() {
  if (destroyed_ || !in_vm_context_created_) {
    return;
  }
  destroyed_ = true;
  onDone();
  onDelete();
}

WasmResult Context::continueStream(WasmStreamType stream_type) {
  switch (stream_type) {
  case WasmStreamType::Request:
    if (decoder_callbacks_) {
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] { decoder_callbacks_->continueDecoding(); });
    }
    break;
  case WasmStreamType::Response:
    if (encoder_callbacks_) {
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] { encoder_callbacks_->continueEncoding(); });
    }
    break;
  case WasmStreamType::Downstream:
    if (network_read_filter_callbacks_) {
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] { network_read_filter_callbacks_->continueReading(); });
    }
    return WasmResult::Ok;
  case WasmStreamType::Upstream:
    return WasmResult::Unimplemented;
  default:
    return WasmResult::BadArgument;
  }
  request_headers_ = nullptr;
  request_body_buffer_ = nullptr;
  request_trailers_ = nullptr;
  request_metadata_ = nullptr;
  return WasmResult::Ok;
}

constexpr absl::string_view CloseStreamResponseDetails = "wasm_close_stream";

WasmResult Context::closeStream(WasmStreamType stream_type) {
  switch (stream_type) {
  case WasmStreamType::Request:
    if (decoder_callbacks_) {
      if (!decoder_callbacks_->streamInfo().responseCodeDetails().has_value()) {
        decoder_callbacks_->streamInfo().setResponseCodeDetails(CloseStreamResponseDetails);
      }
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] { decoder_callbacks_->resetStream(); });
    }
    return WasmResult::Ok;
  case WasmStreamType::Response:
    if (encoder_callbacks_) {
      if (!encoder_callbacks_->streamInfo().responseCodeDetails().has_value()) {
        encoder_callbacks_->streamInfo().setResponseCodeDetails(CloseStreamResponseDetails);
      }
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] { encoder_callbacks_->resetStream(); });
    }
    return WasmResult::Ok;
  case WasmStreamType::Downstream:
    if (network_read_filter_callbacks_) {
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] {
        network_read_filter_callbacks_->connection().close(
            Envoy::Network::ConnectionCloseType::FlushWrite, "wasm_downstream_close");
      });
    }
    return WasmResult::Ok;
  case WasmStreamType::Upstream:
    if (network_write_filter_callbacks_) {
      // We are in a reentrant call, so defer.
      wasm()->addAfterVmCallAction([this] {
        network_write_filter_callbacks_->connection().close(
            Envoy::Network::ConnectionCloseType::FlushWrite, "wasm_upstream_close");
      });
    }
    return WasmResult::Ok;
  }
  return WasmResult::BadArgument;
}

constexpr absl::string_view FailStreamResponseDetails = "wasm_fail_stream";

void Context::failStream(WasmStreamType stream_type) {
  switch (stream_type) {
  case WasmStreamType::Request:
    if (decoder_callbacks_ && !failure_local_reply_sent_) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::ServiceUnavailable, "", nullptr,
                                         Grpc::Status::WellKnownGrpcStatus::Unavailable,
                                         FailStreamResponseDetails);
      failure_local_reply_sent_ = true;
    }
    break;
  case WasmStreamType::Response:
    if (encoder_callbacks_ && !failure_local_reply_sent_) {
      encoder_callbacks_->sendLocalReply(Envoy::Http::Code::ServiceUnavailable, "", nullptr,
                                         Grpc::Status::WellKnownGrpcStatus::Unavailable,
                                         FailStreamResponseDetails);
      failure_local_reply_sent_ = true;
    }
    break;
  case WasmStreamType::Downstream:
    if (network_read_filter_callbacks_) {
      network_read_filter_callbacks_->connection().close(
          Envoy::Network::ConnectionCloseType::FlushWrite);
    }
    break;
  case WasmStreamType::Upstream:
    if (network_write_filter_callbacks_) {
      network_write_filter_callbacks_->connection().close(
          Envoy::Network::ConnectionCloseType::FlushWrite);
    }
    break;
  }
}

WasmResult Context::sendLocalResponse(uint32_t response_code, std::string_view body_text,
                                      Pairs additional_headers, uint32_t grpc_status,
                                      std::string_view details) {
  // "additional_headers" is a collection of string_views. These will no longer
  // be valid when "modify_headers" is finally called below, so we must
  // make copies of all the headers.
  std::vector<std::pair<Http::LowerCaseString, std::string>> additional_headers_copy;
  for (auto& p : additional_headers) {
    const Http::LowerCaseString lower_key{std::string(p.first)};
    additional_headers_copy.emplace_back(lower_key, std::string(p.second));
  }

  auto modify_headers = [additional_headers_copy](Http::HeaderMap& headers) {
    for (auto& p : additional_headers_copy) {
      headers.addCopy(p.first, p.second);
    }
  };

  if (decoder_callbacks_) {
    // This is a bit subtle because proxy_on_delete() does call DeferAfterCallActions(),
    // so in theory it could call this and the Context in the VM would be invalid,
    // but because it only gets called after the connections have drained, the call to
    // sendLocalReply() will fail. Net net, this is safe.
    wasm()->addAfterVmCallAction([this, response_code, body_text = std::string(body_text),
                                  modify_headers = std::move(modify_headers), grpc_status,
                                  details = StringUtil::replaceAllEmptySpace(
                                      absl::string_view(details.data(), details.size()))] {
      // C++, Rust and other SDKs use -1 (InvalidCode) as the default value if gRPC code is not set,
      // which should be mapped to nullopt in Envoy to prevent it from sending a grpc-status trailer
      // at all.
      absl::optional<Grpc::Status::GrpcStatus> grpc_status_code = absl::nullopt;
      if (grpc_status >= Grpc::Status::WellKnownGrpcStatus::Ok &&
          grpc_status <= Grpc::Status::WellKnownGrpcStatus::MaximumKnown) {
        grpc_status_code = Grpc::Status::WellKnownGrpcStatus(grpc_status);
      }
      decoder_callbacks_->sendLocalReply(static_cast<Envoy::Http::Code>(response_code), body_text,
                                         modify_headers, grpc_status_code, details);
    });
  }
  return WasmResult::Ok;
}

Http::FilterHeadersStatus Context::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  onCreate();
  request_headers_ = &headers;
  end_of_stream_ = end_stream;
  auto result = convertFilterHeadersStatus(onRequestHeaders(headerSize(&headers), end_stream));
  if (result == Http::FilterHeadersStatus::Continue) {
    request_headers_ = nullptr;
  }
  return result;
}

Http::FilterDataStatus Context::decodeData(::Envoy::Buffer::Instance& data, bool end_stream) {
  if (!in_vm_context_created_) {
    return Http::FilterDataStatus::Continue;
  }
  if (buffering_request_body_) {
    decoder_callbacks_->addDecodedData(data, false);
    if (destroyed_) {
      // The data adding have triggered a local reply (413) and we needn't to continue to
      // call the VM.
      // Note this is not perfect way. If the local reply processing is stopped by other
      // filters, this filter will still try to call the VM. But at least we can ensure
      // the VM has valid context.
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
  }
  request_body_buffer_ = &data;
  end_of_stream_ = end_stream;
  const auto buffer = getBuffer(WasmBufferType::HttpRequestBody);
  const auto buffer_size = (buffer == nullptr) ? 0 : buffer->size();
  auto result = convertFilterDataStatus(onRequestBody(buffer_size, end_stream));
  buffering_request_body_ = false;
  switch (result) {
  case Http::FilterDataStatus::Continue:
    request_body_buffer_ = nullptr;
    break;
  case Http::FilterDataStatus::StopIterationAndBuffer:
    buffering_request_body_ = true;
    break;
  case Http::FilterDataStatus::StopIterationAndWatermark:
  case Http::FilterDataStatus::StopIterationNoBuffer:
    break;
  }
  return result;
}

Http::FilterTrailersStatus Context::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (!in_vm_context_created_) {
    return Http::FilterTrailersStatus::Continue;
  }
  request_trailers_ = &trailers;
  auto result = convertFilterTrailersStatus(onRequestTrailers(headerSize(&trailers)));
  if (result == Http::FilterTrailersStatus::Continue) {
    request_trailers_ = nullptr;
  }
  return result;
}

Http::FilterMetadataStatus Context::decodeMetadata(Http::MetadataMap& request_metadata) {
  if (!in_vm_context_created_) {
    return Http::FilterMetadataStatus::Continue;
  }
  request_metadata_ = &request_metadata;
  auto result = convertFilterMetadataStatus(onRequestMetadata(headerSize(&request_metadata)));
  if (result == Http::FilterMetadataStatus::Continue) {
    request_metadata_ = nullptr;
  }
  return result;
}

void Context::setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::Filter1xxHeadersStatus Context::encode1xxHeaders(Http::ResponseHeaderMap&) {
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterHeadersStatus Context::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                 bool end_stream) {
  // If the vm context is not created or the stream has failed and the local reply has been sent,
  // we should not continue to call the VM.
  if (!in_vm_context_created_ || failure_local_reply_sent_) {
    return Http::FilterHeadersStatus::Continue;
  }
  response_headers_ = &headers;
  end_of_stream_ = end_stream;
  auto result = convertFilterHeadersStatus(onResponseHeaders(headerSize(&headers), end_stream));
  if (result == Http::FilterHeadersStatus::Continue) {
    response_headers_ = nullptr;
  }
  return result;
}

Http::FilterDataStatus Context::encodeData(::Envoy::Buffer::Instance& data, bool end_stream) {
  // If the vm context is not created or the stream has failed and the local reply has been sent,
  // we should not continue to call the VM.
  if (!in_vm_context_created_ || failure_local_reply_sent_) {
    return Http::FilterDataStatus::Continue;
  }
  if (buffering_response_body_) {
    encoder_callbacks_->addEncodedData(data, false);
    if (destroyed_) {
      // The data adding have triggered a local reply (413) and we needn't to continue to
      // call the VM.
      // Note this is not perfect way. If the local reply processing is stopped by other
      // filters, this filter will still try to call the VM. But at least we can ensure
      // the VM has valid context.
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
  }
  response_body_buffer_ = &data;
  end_of_stream_ = end_stream;
  const auto buffer = getBuffer(WasmBufferType::HttpResponseBody);
  const auto buffer_size = (buffer == nullptr) ? 0 : buffer->size();
  auto result = convertFilterDataStatus(onResponseBody(buffer_size, end_stream));
  buffering_response_body_ = false;
  switch (result) {
  case Http::FilterDataStatus::Continue:
    response_body_buffer_ = nullptr;
    break;
  case Http::FilterDataStatus::StopIterationAndBuffer:
    buffering_response_body_ = true;
    break;
  case Http::FilterDataStatus::StopIterationAndWatermark:
  case Http::FilterDataStatus::StopIterationNoBuffer:
    break;
  }
  return result;
}

Http::FilterTrailersStatus Context::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  // If the vm context is not created or the stream has failed and the local reply has been sent,
  // we should not continue to call the VM.
  if (!in_vm_context_created_ || failure_local_reply_sent_) {
    return Http::FilterTrailersStatus::Continue;
  }
  response_trailers_ = &trailers;
  auto result = convertFilterTrailersStatus(onResponseTrailers(headerSize(&trailers)));
  if (result == Http::FilterTrailersStatus::Continue) {
    response_trailers_ = nullptr;
  }
  return result;
}

Http::FilterMetadataStatus Context::encodeMetadata(Http::MetadataMap& response_metadata) {
  // If the vm context is not created or the stream has failed and the local reply has been sent,
  // we should not continue to call the VM.
  if (!in_vm_context_created_ || failure_local_reply_sent_) {
    return Http::FilterMetadataStatus::Continue;
  }
  response_metadata_ = &response_metadata;
  auto result = convertFilterMetadataStatus(onResponseMetadata(headerSize(&response_metadata)));
  if (result == Http::FilterMetadataStatus::Continue) {
    response_metadata_ = nullptr;
  }
  return result;
}

//  Http::FilterMetadataStatus::Continue;

void Context::setEncoderFilterCallbacks(Envoy::Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

void Context::onHttpCallSuccess(uint32_t token, Envoy::Http::ResponseMessagePtr&& response) {
  // TODO: convert this into a function in proxy-wasm-cpp-host and use here.
  if (proxy_wasm::current_context_ != nullptr) {
    // We are in a reentrant call, so defer.
    wasm()->addAfterVmCallAction([this, token, response = response.release()] {
      onHttpCallSuccess(token, std::unique_ptr<Envoy::Http::ResponseMessage>(response));
    });
    return;
  }
  auto handler = http_request_.find(token);
  if (handler == http_request_.end()) {
    return;
  }
  http_call_response_ = &response;
  uint32_t body_size = response->body().length();
  // Deferred "after VM call" actions are going to be executed upon returning from
  // ContextBase::*, which might include deleting Context object via proxy_done().
  wasm()->addAfterVmCallAction([this, handler] {
    http_call_response_ = nullptr;
    http_request_.erase(handler);
  });
  ContextBase::onHttpCallResponse(token, response->headers().size(), body_size,
                                  headerSize(response->trailers()));
}

void Context::onHttpCallFailure(uint32_t token, Http::AsyncClient::FailureReason reason) {
  if (proxy_wasm::current_context_ != nullptr) {
    // We are in a reentrant call, so defer.
    wasm()->addAfterVmCallAction([this, token, reason] { onHttpCallFailure(token, reason); });
    return;
  }
  auto handler = http_request_.find(token);
  if (handler == http_request_.end()) {
    return;
  }
  status_code_ = static_cast<uint32_t>(WasmResult::BrokenConnection);
  // TODO(botengyao): handle different failure reasons.
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset ||
         reason == Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
  status_message_ = "reset";
  // Deferred "after VM call" actions are going to be executed upon returning from
  // ContextBase::*, which might include deleting Context object via proxy_done().
  wasm()->addAfterVmCallAction([this, handler] {
    status_message_ = "";
    http_request_.erase(handler);
  });
  ContextBase::onHttpCallResponse(token, 0, 0, 0);
}

void Context::onGrpcReceiveWrapper(uint32_t token, ::Envoy::Buffer::InstancePtr response) {
  ASSERT(proxy_wasm::current_context_ == nullptr); // Non-reentrant.
  auto cleanup = [this, token] {
    if (wasm()->isGrpcCallId(token)) {
      grpc_call_request_.erase(token);
    }
  };
  if (wasm()->on_grpc_receive_) {
    grpc_receive_buffer_ = std::move(response);
    uint32_t response_size = grpc_receive_buffer_->length();
    // Deferred "after VM call" actions are going to be executed upon returning from
    // ContextBase::*, which might include deleting Context object via proxy_done().
    wasm()->addAfterVmCallAction([this, cleanup] {
      grpc_receive_buffer_.reset();
      cleanup();
    });
    ContextBase::onGrpcReceive(token, response_size);
  } else {
    cleanup();
  }
}

void Context::onGrpcCloseWrapper(uint32_t token, const Grpc::Status::GrpcStatus& status,
                                 const std::string_view message) {
  if (proxy_wasm::current_context_ != nullptr) {
    // We are in a reentrant call, so defer.
    wasm()->addAfterVmCallAction([this, token, status, message = std::string(message)] {
      onGrpcCloseWrapper(token, status, message);
    });
    return;
  }
  auto cleanup = [this, token] {
    if (wasm()->isGrpcCallId(token)) {
      grpc_call_request_.erase(token);
    } else if (wasm()->isGrpcStreamId(token)) {
      auto it = grpc_stream_.find(token);
      if (it != grpc_stream_.end()) {
        if (it->second.local_closed_) {
          grpc_stream_.erase(token);
        }
      }
    }
  };
  if (wasm()->on_grpc_close_) {
    status_code_ = static_cast<uint32_t>(status);
    status_message_ = toAbslStringView(message);
    // Deferred "after VM call" actions are going to be executed upon returning from
    // ContextBase::*, which might include deleting Context object via proxy_done().
    wasm()->addAfterVmCallAction([this, cleanup] {
      status_message_ = "";
      cleanup();
    });
    ContextBase::onGrpcClose(token, status_code_);
  } else {
    cleanup();
  }
}

WasmResult Context::grpcSend(uint32_t token, std::string_view message, bool end_stream) {
  if (!wasm()->isGrpcStreamId(token)) {
    return WasmResult::BadArgument;
  }
  auto it = grpc_stream_.find(token);
  if (it == grpc_stream_.end()) {
    return WasmResult::NotFound;
  }
  if (it->second.stream_) {
    it->second.stream_->sendMessageRaw(::Envoy::Buffer::InstancePtr(new ::Envoy::Buffer::OwnedImpl(
                                           message.data(), message.size())),
                                       end_stream);
  }
  return WasmResult::Ok;
}

WasmResult Context::grpcClose(uint32_t token) {
  if (wasm()->isGrpcCallId(token)) {
    auto it = grpc_call_request_.find(token);
    if (it == grpc_call_request_.end()) {
      return WasmResult::NotFound;
    }
    if (it->second.request_) {
      it->second.request_->cancel();
    }
    grpc_call_request_.erase(token);
    return WasmResult::Ok;
  } else if (wasm()->isGrpcStreamId(token)) {
    auto it = grpc_stream_.find(token);
    if (it == grpc_stream_.end()) {
      return WasmResult::NotFound;
    }
    if (it->second.stream_) {
      it->second.stream_->closeStream();
    }
    if (it->second.remote_closed_) {
      grpc_stream_.erase(token);
    } else {
      it->second.local_closed_ = true;
    }
    return WasmResult::Ok;
  }
  return WasmResult::BadArgument;
}

WasmResult Context::grpcCancel(uint32_t token) {
  if (wasm()->isGrpcCallId(token)) {
    auto it = grpc_call_request_.find(token);
    if (it == grpc_call_request_.end()) {
      return WasmResult::NotFound;
    }
    if (it->second.request_) {
      it->second.request_->cancel();
    }
    grpc_call_request_.erase(token);
    return WasmResult::Ok;
  } else if (wasm()->isGrpcStreamId(token)) {
    auto it = grpc_stream_.find(token);
    if (it == grpc_stream_.end()) {
      return WasmResult::NotFound;
    }
    if (it->second.stream_) {
      it->second.stream_->resetStream();
    }
    grpc_stream_.erase(token);
    return WasmResult::Ok;
  }
  return WasmResult::BadArgument;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
