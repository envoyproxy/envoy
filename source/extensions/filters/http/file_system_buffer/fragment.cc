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

absl::StatusOr<CancelFunction> Fragment::toStorage(AsyncFileHandle file, off_t offset,
                                                   Event::Dispatcher& dispatcher,
                                                   absl::AnyInvocable<void(absl::Status)> on_done) {
  ASSERT(isMemory());
  auto data = absl::get<MemoryFragment>(data_).extract();
  data_.emplace<WritingFragment>();
  // This callback is only called if the filter was not destroyed in the meantime,
  // so it is safe to use `this`.
  return file->write(
      &dispatcher, *data, offset,
      [this, on_done = std::move(on_done), offset](absl::StatusOr<size_t> result) mutable {
        if (!result.ok()) {
          std::move(on_done)(result.status());
        } else if (result.value() != size_) {
          auto status = absl::AbortedError(
              fmt::format("buffer write wrote {} bytes, wanted {}", result.value(), size_));
          std::move(on_done)(status);
        } else {
          data_.emplace<StorageFragment>(offset);
          std::move(on_done)(absl::OkStatus());
        }
      });
}

absl::StatusOr<CancelFunction>
Fragment::fromStorage(AsyncFileHandle file, Event::Dispatcher& dispatcher,
                      absl::AnyInvocable<void(absl::Status)> on_done) {
  ASSERT(isStorage());
  off_t offset = absl::get<StorageFragment>(data_).offset();
  data_.emplace<ReadingFragment>();
  // This callback is only called if the filter was not destroyed in the meantime,
  // so it is safe to use `this`.
  return file->read(&dispatcher, offset, size_,
                    [this, on_done = std::move(on_done)](
                        absl::StatusOr<std::unique_ptr<Buffer::Instance>> result) mutable {
                      if (!result.ok()) {
                        std::move(on_done)(result.status());
                      } else if (result.value()->length() != size_) {
                        auto status =
                            absl::AbortedError(fmt::format("buffer read got {} bytes, wanted {}",
                                                           result.value()->length(), size_));
                        std::move(on_done)(status);
                      } else {
                        data_.emplace<MemoryFragment>(*result.value());
                        std::move(on_done)(absl::OkStatus());
                      }
                    });
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
