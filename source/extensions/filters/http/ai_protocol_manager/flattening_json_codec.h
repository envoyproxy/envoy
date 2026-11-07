#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/task.h"
#include "source/common/json/wuffs_json/wuffs_json_cursor.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Path from the root of a JSON document to a leaf field: object keys are strings and array
// indices are size_t values.
using FieldPathSegment = absl::variant<std::string, size_t>;
using FieldPath = absl::Span<const FieldPathSegment>;

// Represents a flattened leaf JSON field (`path -> leaf node`) or a partial chunk of a
// string/binary field that spans multiple input buffers.
class FlattenJsonField {
public:
  FlattenJsonField() = default;

  FlattenJsonField(std::vector<FieldPathSegment> path, nlohmann::json node, bool is_partial = false)
      : path_(std::move(path)), node_(std::move(node)), is_partial_(is_partial) {}

  FieldPath field_path() const { return path_; }

  void set_field_path(std::vector<FieldPathSegment> path) { path_ = std::move(path); }

  void set_field_path(FieldPath path) { path_.assign(path.begin(), path.end()); }

  // The node is guaranteed to be a leaf type in nlohmann::json::value_t:
  // null, boolean, number_*, string, binary (or an empty object/array container leaf).
  // For string and binary, this might be a partial chunk of the field when `is_partial()` is true.
  const nlohmann::json& node() const { return node_; }
  nlohmann::json& node() { return node_; }

  // True when `node()` holds an incomplete string/binary chunk whose closing quote has not yet
  // arrived in the current `onData` call; false once the string completes and on all non-partial
  // leaf values.
  bool is_partial() const { return is_partial_; }
  void set_is_partial(bool is_partial) { is_partial_ = is_partial; }

  // Estimated byte footprint for watermark accounting when queued between stages.
  uint64_t byteSize() const;

  bool operator==(const FlattenJsonField& other) const {
    return path_ == other.path_ && node_ == other.node_ && is_partial_ == other.is_partial_;
  }

private:
  std::vector<FieldPathSegment> path_;
  nlohmann::json node_;
  bool is_partial_{false};
};

// Incrementally flattens a JSON byte stream into `FlattenJsonField` `(FieldPath, leaf node)`
// pairs.
//
// Driven by `Json::Wuffs::WuffsJsonCursor` so incoming `Buffer::Instance` slices can split at any
// byte boundary. Leaf scalars (null, boolean, number, string) and empty containers (`{}` and `[]`)
// are emitted with their root-to-leaf `field_path()`. When a string value spans multiple `onData`
// calls, each `onData` call emits the decoded portion accumulated during that call with
// `is_partial() == true`, and the `onData` call that sees the closing quote emits the final chunk
// with `is_partial() == false`.
class FlatteningJsonDecoder : public Json::Wuffs::WuffsJsonCursor::Handler {
public:
  FlatteningJsonDecoder();

  // Decodes `data` and returns every flattened `FlattenJsonField` produced during this call. When
  // `end_stream` is true, also validates that the JSON document is complete.
  absl::StatusOr<std::vector<FlattenJsonField>> onData(const Buffer::Instance& data,
                                                       bool end_stream = false);

  // Json::Wuffs::WuffsJsonCursor::Handler
  bool openStringCapture(absl::string_view key, int depth, size_t token_start) override;
  bool onStringChunk(absl::string_view key, int depth, absl::string_view chunk) override;
  void closeStringCapture(absl::string_view key, int depth, size_t token_end) override;
  absl::Status onKey(absl::string_view key, int depth, size_t token_start) override;
  absl::Status onNumber(absl::string_view key, absl::string_view raw, int depth, size_t token_start,
                        size_t token_end) override;
  absl::Status onBoolean(absl::string_view key, bool value, int depth, size_t token_start,
                         size_t token_end) override;
  void onNull(absl::string_view key, int depth, size_t token_start, size_t token_end) override;
  void onContainerOpen(absl::string_view key, bool is_dict, int depth, size_t token_start) override;
  void onContainerClose(int depth, size_t token_end) override;

private:
  struct ContainerState {
    FieldPathSegment segment;
    bool is_dict{false};
    bool has_children{false};
  };

  absl::Status feedCursor(absl::string_view data, bool closed);
  void advanceChildSegment(absl::string_view key);
  absl::Status emitLeafValue(absl::string_view key, nlohmann::json value);
  void emitCurrentPathField(nlohmann::json value, bool is_partial = false);
  void setError(absl::Status status);

  Json::Wuffs::WuffsJsonCursor cursor_;

  std::vector<ContainerState> container_stack_;
  std::vector<FlattenJsonField> current_fields_;
  bool in_string_{false};

  bool root_seen_{false};
  bool finished_{false};
  absl::Status status_;
};

// Re-serializes a stream of `FlattenJsonField` batches into compact JSON through a
// `BufferManager`.
//
// Maintains an explicit stack of open containers across batches, closing completed containers and
// opening new ones from the prefix diff between consecutive `field_path()`s. Consecutive partial
// string `FlattenJsonField`s (`is_partial() == true`) followed by their final chunk
// (`is_partial() == false`) at the identical `field_path()` are concatenated inside a single JSON
// string literal (`"..."`).
class FlatteningJsonSerializer {
public:
  explicit FlatteningJsonSerializer(BufferManager& out);

  // Serializes a batch of `FlattenJsonField`s into the output buffer manager.
  Coroutine::Task<absl::Status> serializeBatch(absl::Span<const FlattenJsonField> fields);

  // Closes any remaining open string and container scopes and flushes the output buffer.
  Coroutine::Task<absl::Status> finish();

private:
  struct OpenContainer {
    bool is_object{false};
    FieldPathSegment segment;
  };

  static constexpr size_t kMaxSmallBufferSize = 4096;

  bool pathMatchesOpenContainers(FieldPath path) const;
  Coroutine::Task<absl::Status> serializeField(const FlattenJsonField& field);
  Coroutine::Task<absl::Status> maybeFlushBuffer();
  Coroutine::Task<absl::Status> flushBuffer();

  BufferManager& out_;
  Buffer::OwnedImpl small_buf_;
  std::vector<OpenContainer> open_containers_;
  bool string_open_{false};
  bool root_emitted_{false};
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
