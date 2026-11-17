#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/replay_awaitable.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

absl::StatusOr<std::string> dumpJson(const nlohmann::json& node) noexcept {
  TRY_NEEDS_AUDIT { return node.dump(); }
  END_TRY
  CATCH(const nlohmann::json::exception& e, {
    return absl::InvalidArgumentError(absl::StrCat("JSON serialization error: ", e.what()));
  })
  CATCH(const std::exception& e, {
    return absl::InvalidArgumentError(absl::StrCat("JSON serialization error: ", e.what()));
  })
  CATCH(..., { return absl::InvalidArgumentError("unknown error during JSON serialization"); })
  return absl::InternalError("unexpected flow in dumpJson");
}

enum class SerializationMode {
  Counting,
  Emit,
};

class SerializerImpl {
public:
  SerializerImpl(BufferManager* out, BufferManager* ref_source, SerializationMode mode)
      : out_(out), ref_source_(ref_source), mode_(mode) {}

  Coroutine::Task<absl::Status> serialize(const nlohmann::json& node, nlohmann::json& new_node) {
    CO_RETURN_IF_ERROR(co_await serializeNode(node, new_node));
    co_return co_await flushBuffer();
  }

  uint64_t totalBytes() const { return byte_counter_; }

private:
  static constexpr size_t kMaxSmallBufferSize = 4096;

  Coroutine::Task<absl::Status> maybeFlushBuffer() {
    if (small_buf_.length() >= kMaxSmallBufferSize) {
      co_return co_await flushBuffer();
    }
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> flushBuffer() {
    if (small_buf_.length() > 0) {
      byte_counter_ += small_buf_.length();
      switch (mode_) {
      case SerializationMode::Emit:
        // TODO(penguingao): if the replay becomes too fragmented between
        // external buffer and re-serialization, we could change the interface to
        // BufferManager take hint from the serializer's potential next replay
        // ranges, this way, it can then internally coalescing reads to save
        // I/O.
        CO_RETURN_IF_ERROR(co_await ReplayAwaitable(*out_, small_buf_));
        break;
      case SerializationMode::Counting:
        small_buf_.drain(small_buf_.length());
        break;
      }
    }
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> serializeExternalRef(const nlohmann::json& node,
                                                     nlohmann::json& new_node) {
    ASSIGN_OR_CO_RETURN(const JsonWithExtBuf::ExternalRef ref, JsonWithExtBuf::externalRef(node));
    if (mode_ == SerializationMode::Emit) {
      if (ref_source_ == nullptr) {
        co_return absl::InternalError("no buffer manager holds the bytes an ExternalRef names");
      }
      if (ref.offset > ref_source_->length() || ref.length > ref_source_->length() - ref.offset) {
        co_return absl::InvalidArgumentError(
            absl::StrCat("external buffer reference [", ref.offset, ", ", ref.offset + ref.length,
                         ") exceeds buffer length ", ref_source_->length()));
      }
    }
    small_buf_.add("\"");
    uint64_t new_offset = byte_counter_ + small_buf_.length();
    new_node = JsonWithExtBuf::makeExternalRef({new_offset, ref.length});
    if (ref.length > 0) {
      CO_RETURN_IF_ERROR(co_await flushBuffer());
      byte_counter_ += ref.length;
      switch (mode_) {
      case SerializationMode::Emit:
        CO_RETURN_IF_ERROR(co_await ReplayAwaitable(*ref_source_, ref.offset, ref.length));
        break;
      case SerializationMode::Counting:
        break;
      }
    }
    small_buf_.add("\"");
    CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> serializeNode(const nlohmann::json& node,
                                              nlohmann::json& new_node) {
    if (JsonWithExtBuf::isExternalRef(node)) {
      co_return co_await serializeExternalRef(node, new_node);
    }

    switch (node.type()) {
    case nlohmann::json::value_t::null:
      small_buf_.add("null");
      new_node = nullptr;
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    case nlohmann::json::value_t::boolean:
      small_buf_.add(node.get<bool>() ? "true" : "false");
      new_node = node.get<bool>();
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned:
    case nlohmann::json::value_t::number_float:
    case nlohmann::json::value_t::string: {
      ASSIGN_OR_CO_RETURN(const std::string dumped, dumpJson(node));
      small_buf_.add(dumped);
      new_node = node;
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    }
    case nlohmann::json::value_t::array: {
      new_node = nlohmann::json::array();
      small_buf_.add("[");
      bool first = true;
      for (const auto& item : node) {
        if (!first) {
          small_buf_.add(",");
        }
        first = false;
        nlohmann::json child;
        CO_RETURN_IF_ERROR(co_await serializeNode(item, child));
        new_node.push_back(std::move(child));
      }
      small_buf_.add("]");
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    }
    case nlohmann::json::value_t::object: {
      new_node = nlohmann::json::object();
      small_buf_.add("{");
      bool first = true;
      for (auto it = node.begin(); it != node.end(); ++it) {
        if (!first) {
          small_buf_.add(",");
        }
        first = false;
        ASSIGN_OR_CO_RETURN(const std::string dumped_key, dumpJson(nlohmann::json(it.key())));
        small_buf_.add(dumped_key);
        small_buf_.add(":");
        nlohmann::json child;
        CO_RETURN_IF_ERROR(co_await serializeNode(it.value(), child));
        new_node[it.key()] = std::move(child);
      }
      small_buf_.add("}");
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    }
    case nlohmann::json::value_t::binary: {
      ASSIGN_OR_CO_RETURN(const std::string dumped, dumpJson(node));
      small_buf_.add(dumped);
      new_node = node;
      CO_RETURN_IF_ERROR(co_await maybeFlushBuffer());
      break;
    }
    case nlohmann::json::value_t::discarded:
      co_return absl::InvalidArgumentError("cannot serialize discarded JSON node");
    }

    co_return absl::OkStatus();
  }

  BufferManager* out_{nullptr};
  BufferManager* ref_source_{nullptr};
  SerializationMode mode_{SerializationMode::Emit};
  Buffer::OwnedImpl small_buf_;
  uint64_t byte_counter_{0};
};

} // namespace

Coroutine::Task<absl::StatusOr<Serializer::SerializedOffsets>>
Serializer::calculateSerializedOffsets(const JsonWithExtBuf& doc) {
  SerializerImpl impl(nullptr, nullptr, SerializationMode::Counting);
  nlohmann::json new_json;
  CO_RETURN_IF_ERROR(co_await impl.serialize(doc.json(), new_json));

  JsonWithExtBuf new_doc;
  new_doc.setJson(std::move(new_json));
  co_return SerializedOffsets{std::move(new_doc), impl.totalBytes()};
}

Coroutine::Task<absl::StatusOr<JsonWithExtBuf>>
Serializer::serialize(const JsonWithExtBuf& doc, BufferManager* out, BufferManager* ref_source) {
  if (out == nullptr) {
    co_return absl::InvalidArgumentError("out must not be null for serialize");
  }
  SerializerImpl impl(out, ref_source, SerializationMode::Emit);
  nlohmann::json new_json;
  CO_RETURN_IF_ERROR(co_await impl.serialize(doc.json(), new_json));

  JsonWithExtBuf new_doc;
  new_doc.setJson(std::move(new_json));
  co_return new_doc;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
