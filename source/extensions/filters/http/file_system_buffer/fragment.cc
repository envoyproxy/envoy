#include "source/extensions/filters/http/file_system_buffer/fragment.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

class FragmentData {
public:
  virtual bool isMemory() const { return false; }
  virtual bool isStorage() const { return false; }
  virtual ~FragmentData() = default;
};

class MemoryFragment : public FragmentData {
public:
  explicit MemoryFragment(Buffer::Instance& buffer); // NOLINT(runtime/references)
  explicit MemoryFragment(Buffer::Instance& buffer,
                          size_t size); // NOLINT(runtime/references)
  std::unique_ptr<Buffer::Instance> extract();
  bool isMemory() const override { return true; }

private:
  std::unique_ptr<Buffer::OwnedImpl> buffer_;
};

class WritingFragment : public FragmentData {};

class ReadingFragment : public FragmentData {};

class StorageFragment : public FragmentData {
public:
  explicit StorageFragment(off_t offset) : offset_(offset) {}
  off_t offset() const { return offset_; }
  bool isStorage() const override { return true; }

private:
  const off_t offset_;
};

Fragment::Fragment(Buffer::Instance& buffer) // NOLINT(runtime/references)
    : size_(buffer.length()), data_(std::make_unique<MemoryFragment>(buffer)) {}

Fragment::Fragment(Buffer::Instance& buffer, size_t size) // NOLINT(runtime/references)
    : size_(size), data_(std::make_unique<MemoryFragment>(buffer, size)) {}

Fragment::~Fragment() = default;
bool Fragment::isMemory() const { return data_->isMemory(); }
bool Fragment::isStorage() const { return data_->isStorage(); }

MemoryFragment::MemoryFragment(Buffer::Instance& buffer) // NOLINT(runtime/references)
    : buffer_(std::make_unique<Buffer::OwnedImpl>()) {
  buffer_->move(buffer);
}

MemoryFragment::MemoryFragment(Buffer::Instance& buffer,
                               size_t size) // NOLINT(runtime/references)
    : buffer_(std::make_unique<Buffer::OwnedImpl>()) {
  buffer_->move(buffer, size);
}

std::unique_ptr<Buffer::Instance> Fragment::extract() {
  ASSERT(isMemory());
  auto ret = dynamic_cast<MemoryFragment*>(data_.get())->extract();
  data_.reset();
  size_ = 0;
  return ret;
}

std::unique_ptr<Buffer::Instance> MemoryFragment::extract() { return std::move(buffer_); }

absl::StatusOr<CancelFunction>
Fragment::toStorage(AsyncFileHandle file, off_t offset,
                    std::function<void(absl::StatusOr<UpdateFragmentFunction>)> on_done) {
  ASSERT(isMemory());
  auto data = dynamic_cast<MemoryFragment*>(data_.get())->extract();
  data_ = std::make_unique<WritingFragment>();
  return file->write(*data, offset,
                     [fragment = this, size = size_, offset,
                      on_done = std::move(on_done)](absl::StatusOr<size_t> result) {
                       // This lambda always runs in the async file thread, so must not use the
                       // fragment directly as it may have been destroyed. It is passed through to
                       // call from on_done's callback, where it can be used again as that function
                       // is called from the envoy thread only if the filter has *not* been
                       // destroyed in the meantime.
                       if (!result.ok()) {
                         on_done(result.status());
                         return;
                       }
                       if (result.value() != size) {
                         on_done(absl::AbortedError(fmt::format(
                             "buffer write wrote {} bytes, wanted {}", result.value(), size)));
                         return;
                       }
                       on_done([fragment, offset]() {
                         fragment->data_ = std::make_unique<StorageFragment>(offset);
                       });
                     });
}

absl::StatusOr<CancelFunction>
Fragment::fromStorage(AsyncFileHandle file,
                      std::function<void(absl::StatusOr<UpdateFragmentFunction>)> on_done) {
  ASSERT(isStorage());
  off_t offset = dynamic_cast<StorageFragment*>(data_.get())->offset();
  data_ = std::make_unique<ReadingFragment>();
  return file->read(
      offset, size_,
      [fragment = this, size = size_,
       on_done](absl::StatusOr<std::unique_ptr<Buffer::Instance>> result) {
        // This lambda always runs in the async file thread, so must not use the
        // fragment directly as it may have been destroyed. It is passed through to
        // call from on_done's callback, where it can be used again as that function
        // is called from the envoy thread only if the filter has *not* been destroyed
        // in the meantime.
        if (!result.ok()) {
          on_done(result.status());
          return;
        }
        if (result.value()->length() != size) {
          on_done(absl::AbortedError(
              fmt::format("buffer read got {} bytes, wanted {}", result.value()->length(), size)));
          return;
        }
        on_done([fragment, data = std::shared_ptr<Buffer::Instance>(result.value().release())]() {
          fragment->data_ = std::make_unique<MemoryFragment>(*data);
        });
      });
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
