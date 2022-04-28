#include "source/extensions/filters/http/file_system_buffer/fragment.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

Fragment::Fragment(Buffer::Instance& buffer)
    : size_(buffer.length()), data_(MemoryFragment(buffer)) {}

Fragment::Fragment(Buffer::Instance& buffer, size_t size)
    : size_(size), data_(MemoryFragment(buffer, size)) {}

Fragment::~Fragment() = default;
bool Fragment::isMemory() const { return absl::holds_alternative<MemoryFragment>(data_); }
bool Fragment::isStorage() const { return absl::holds_alternative<StorageFragment>(data_); }

MemoryFragment::MemoryFragment(Buffer::Instance& buffer)
    : buffer_(std::make_unique<Buffer::OwnedImpl>()) {
  buffer_->move(buffer);
}

MemoryFragment::MemoryFragment(Buffer::Instance& buffer, size_t size)
    : buffer_(std::make_unique<Buffer::OwnedImpl>()) {
  buffer_->move(buffer, size);
}

std::unique_ptr<Buffer::Instance> Fragment::extract() {
  auto ret = absl::get<MemoryFragment>(data_).extract();
  size_ = 0;
  return ret;
}

std::unique_ptr<Buffer::Instance> MemoryFragment::extract() { return std::move(buffer_); }

absl::StatusOr<CancelFunction>
Fragment::toStorage(AsyncFileHandle file, off_t offset,
                    std::function<void(std::function<void()>)> dispatch,
                    std::function<void(absl::Status)> on_done) {
  ASSERT(isMemory());
  auto data = absl::get<MemoryFragment>(data_).extract();
  data_.emplace<WritingFragment>();
  return file->write(
      *data, offset,
      [this, dispatch = std::move(dispatch), size = size_, on_done = std::move(on_done),
       offset](absl::StatusOr<size_t> result) {
        // size is captured because we can't safely use 'this' until we're in the dispatch callback.
        if (!result.ok()) {
          dispatch([status = result.status(), on_done = std::move(on_done)]() { on_done(status); });
        } else if (result.value() != size) {
          auto status = absl::AbortedError(
              fmt::format("buffer write wrote {} bytes, wanted {}", result.value(), size));
          dispatch(
              [on_done = std::move(on_done), status = std::move(status)]() { on_done(status); });
        } else {
          dispatch([this, status = result.status(), offset, on_done = std::move(on_done)] {
            data_.emplace<StorageFragment>(offset);
            on_done(absl::OkStatus());
          });
        }
      });
}

absl::StatusOr<CancelFunction>
Fragment::fromStorage(AsyncFileHandle file, std::function<void(std::function<void()>)> dispatch,
                      std::function<void(absl::Status)> on_done) {
  ASSERT(isStorage());
  off_t offset = absl::get<StorageFragment>(data_).offset();
  data_.emplace<ReadingFragment>();
  return file->read(
      offset, size_,
      [this, dispatch = std::move(dispatch), size = size_,
       on_done = std::move(on_done)](absl::StatusOr<std::unique_ptr<Buffer::Instance>> result) {
        // size is captured because we can't safely use 'this' until we're in the dispatch callback.
        if (!result.ok()) {
          dispatch([on_done = std::move(on_done), status = result.status()]() { on_done(status); });
        } else if (result.value()->length() != size) {
          auto status = absl::AbortedError(
              fmt::format("buffer read got {} bytes, wanted {}", result.value()->length(), size));
          dispatch(
              [on_done = std::move(on_done), status = std::move(status)]() { on_done(status); });
        } else {
          auto buffer = std::shared_ptr<Buffer::Instance>(std::move(result.value()));
          dispatch([this, on_done = std::move(on_done), buffer = std::move(buffer)]() {
            data_.emplace<MemoryFragment>(*buffer);
            on_done(absl::OkStatus());
          });
        }
      });
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
