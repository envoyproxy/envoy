#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"

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

class ReplayAwaitable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  ReplayAwaitable(BufferManager& buffer_manager, uint64_t offset, uint64_t length)
      : buffer_manager_(buffer_manager), offset_(offset), length_(length), is_in_memory_(false) {}

  ReplayAwaitable(BufferManager& buffer_manager, Buffer::Instance& data)
      : buffer_manager_(buffer_manager), is_in_memory_(true) {
    data_.move(data);
  }

protected:
  // Fast path: complete immediately without suspending if nothing needs to be replayed.
  std::optional<absl::Status> tryImmediate() override {
    if ((is_in_memory_ && data_.length() == 0) || (!is_in_memory_ && length_ == 0)) {
      return absl::OkStatus();
    }
    return std::nullopt;
  }

  // TODO(penguingao): Consider updating LeafAwaitable::onStart to return a bool (indicating
  // whether to suspend) so that if buffer_manager_.replay completes synchronously on-stack, we can
  // avoid await_suspend as defense-in-depth without splitting state across tryImmediate.
  void onStart() override {
    if (is_in_memory_) {
      buffer_manager_.replay(data_, [this]() { complete(absl::OkStatus()); });
    } else {
      buffer_manager_.replay(offset_, length_, [this]() { complete(absl::OkStatus()); });
    }
  }

  void onCancel() override { buffer_manager_.cancelReplay(); }

private:
  BufferManager& buffer_manager_;
  uint64_t offset_{0};
  uint64_t length_{0};
  bool is_in_memory_{false};
  Buffer::OwnedImpl data_;
};

class SerializerImpl {
public:
  SerializerImpl(BufferManager* buffer_manager, SerializationMode mode)
      : buffer_manager_(buffer_manager), mode_(mode) {}

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
        if (buffer_manager_ == nullptr) {
          co_return absl::InternalError("buffer_manager is null during flushBuffer");
        }
        // TODO(penguingao): if the replay becomes too fragmented between
        // external buffer and reserialization, we could change the interface to
        // BufferManager take hint from the serializer's potential next replay
        // ranges, this way, it can then internally coalescing reads to save
        // I/O.
        CO_RETURN_IF_ERROR(co_await ReplayAwaitable(*buffer_manager_, small_buf_));
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
      if (buffer_manager_ == nullptr) {
        co_return absl::InternalError("buffer_manager is null for ExternalRef node");
      }
      if (ref.offset > buffer_manager_->length() ||
          ref.length > buffer_manager_->length() - ref.offset) {
        co_return absl::InvalidArgumentError(
            absl::StrCat("external buffer reference [", ref.offset, ", ", ref.offset + ref.length,
                         ") exceeds buffer length ", buffer_manager_->length()));
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
        CO_RETURN_IF_ERROR(co_await ReplayAwaitable(*buffer_manager_, ref.offset, ref.length));
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

  BufferManager* buffer_manager_{nullptr};
  SerializationMode mode_{SerializationMode::Emit};
  Buffer::OwnedImpl small_buf_;
  uint64_t byte_counter_{0};
};

} // namespace

Coroutine::Task<absl::StatusOr<Serializer::SerializedOffsets>>
Serializer::calculateSerializedOffsets(const JsonWithExtBuf& doc) {
  SerializerImpl impl(nullptr, SerializationMode::Counting);
  nlohmann::json new_json;
  CO_RETURN_IF_ERROR(co_await impl.serialize(doc.json(), new_json));

  JsonWithExtBuf new_doc;
  new_doc.setJson(std::move(new_json));
  co_return SerializedOffsets{std::move(new_doc), impl.totalBytes()};
}

Coroutine::Task<absl::StatusOr<JsonWithExtBuf>>
Serializer::serialize(const JsonWithExtBuf& doc, BufferManager* buffer_manager) {
  if (buffer_manager == nullptr) {
    co_return absl::InvalidArgumentError("buffer_manager must not be null for serialize");
  }
  SerializerImpl impl(buffer_manager, SerializationMode::Emit);
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
