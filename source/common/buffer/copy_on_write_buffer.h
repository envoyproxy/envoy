#pragma once

#include <atomic>
#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Buffer {

/**
 * Simple wrapper around a buffer instance to enable sharing
 * between multiple copy-on-write buffers.
 */
class SharedBuffer {
public:
  explicit SharedBuffer(std::unique_ptr<Instance> buffer) : buffer_(std::move(buffer)) {}

  /**
   * @return const reference to the underlying buffer for read operations.
   */
  const Instance& buffer() const { return *buffer_; }

  /**
   * Get mutable access to the buffer. Should only be called when exclusively owned
   * to ensure exclusive access.
   * @return mutable reference to the underlying buffer.
   */
  Instance& mutableBuffer() { return *buffer_; }

private:
  std::unique_ptr<Instance> buffer_;
};

using SharedBufferPtr = std::shared_ptr<SharedBuffer>;

/**
 * Copy-on-write buffer implementation that shares underlying data until modifications occur.
 * This optimizes memory usage when multiple upstream requests need to process the same
 * large request body with minimal or no modifications.
 */
class CopyOnWriteBuffer : public Instance, Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * Create a new copy-on-write buffer that shares data with an existing buffer.
   * @param shared_buffer the shared buffer to reference.
   */
  explicit CopyOnWriteBuffer(SharedBufferPtr shared_buffer);

  /**
   * Create a new copy-on-write buffer from an existing buffer instance.
   * @param source_buffer the buffer to wrap in a shared reference.
   */
  explicit CopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer);

  /**
   * Copy constructor creates a new copy-on-write buffer sharing the same underlying data.
   */
  CopyOnWriteBuffer(const CopyOnWriteBuffer& other);

  /**
   * Assignment operator shares the underlying data.
   */
  CopyOnWriteBuffer& operator=(const CopyOnWriteBuffer& other);

  ~CopyOnWriteBuffer() override;

  // Buffer::Instance interface - read operations delegate to shared buffer.
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  void move(Instance& rhs, uint64_t length, bool reset_drain_trackers_and_accounting) override;
  ssize_t search(const void* data, uint64_t size, size_t start, size_t length) const override;
  bool startsWith(absl::string_view data) const override;
  std::string toString() const override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  uint64_t copyOutToSlices(uint64_t size, RawSlice* slices, uint64_t num_slice) const override;
  RawSliceVector getRawSlices(absl::optional<uint64_t> max_slices = absl::nullopt) const override;
  RawSlice frontSlice() const override;
  SliceDataPtr extractMutableFrontSlice() override;

  // Buffer::Instance interface - write operations trigger copy-on-write.
  void add(const void* data, uint64_t size) override;
  void addBufferFragment(BufferFragment& fragment) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void drain(uint64_t size) override;
  void addDrainTracker(std::function<void()> drain_tracker) override;
  void bindAccount(BufferMemoryAccountSharedPtr account) override;
  Reservation reserveForRead() override;
  ReservationSingleSlice reserveSingleSlice(uint64_t length, bool separate_slice = false) override;
  size_t addFragments(absl::Span<const absl::string_view> fragments) override;

  // Watermark operations - not implemented for copy-on-write buffers.
  void setWatermarks(uint32_t, uint32_t) override {
    RELEASE_ASSERT(false, "Copy-on-write buffers do not support watermarks");
  }
  uint32_t highWatermark() const override { return 0; }
  bool highWatermarkTriggered() const override { return false; }

  /**
   * @return true if this copy-on-write buffer shares data with other instances.
   */
  bool isShared() const;

  /**
   * @return the number of references to the underlying shared data.
   */
  uint32_t referenceCount() const;

protected:
  void commit(uint64_t length, absl::Span<RawSlice> slices,
              ReservationSlicesOwnerPtr slices_owner) override;

private:
  /**
   * Trigger copy-on-write if the buffer is currently shared.
   * After this call, private_buffer_ will contain an exclusive copy of the data.
   */
  void ensurePrivateBuffer();

  // Either we have a shared buffer (for read-only access).
  SharedBufferPtr shared_buffer_;

  // Or we have a private buffer (after copy-on-write).
  std::unique_ptr<Instance> private_buffer_;

  // Account for memory tracking.
  BufferMemoryAccountSharedPtr account_;
};

/**
 * Factory function to create a copy-on-write buffer from an existing buffer.
 * This is the primary interface for creating copy-on-write buffers in the upstream filter chain.
 */
std::unique_ptr<CopyOnWriteBuffer> createCopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer);

/**
 * Factory function to create multiple shared copy-on-write buffers from a single source buffer.
 * All returned copy-on-write buffers will share the same underlying data until one modifies it.
 * @param source_buffer the buffer to share.
 * @param count the number of copy-on-write buffers to create.
 * @return vector of copy-on-write buffers sharing the same underlying data.
 */
std::vector<std::unique_ptr<CopyOnWriteBuffer>>
createSharedCopyOnWriteBuffers(std::unique_ptr<Instance> source_buffer, size_t count);

} // namespace Buffer
} // namespace Envoy
