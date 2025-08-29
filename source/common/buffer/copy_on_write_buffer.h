#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Buffer {

/**
 * Simple wrapper around a buffer instance to enable sharing between multiple copy-on-write buffers.
 */
class SharedBuffer {
public:
  explicit SharedBuffer(std::unique_ptr<Instance> buffer) : buffer_(std::move(buffer)) {}

  /**
   * @return const reference to the underlying buffer for read operations.
   */
  const Instance& buffer() const { return *buffer_; }

private:
  std::unique_ptr<Instance> buffer_;
};

using SharedBufferPtr = std::shared_ptr<SharedBuffer>;

/**
 * Copy-on-write buffer implementation that shares underlying data until modifications occur.
 * This optimizes memory usage when multiple consumers read the same large request body.
 */
class CopyOnWriteBuffer : public Instance {
public:
  explicit CopyOnWriteBuffer(SharedBufferPtr shared_buffer);
  explicit CopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer);
  CopyOnWriteBuffer(const CopyOnWriteBuffer& other);
  CopyOnWriteBuffer& operator=(const CopyOnWriteBuffer& other);
  ~CopyOnWriteBuffer() override;

  // Buffer::Instance read interface.
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

  // Buffer::Instance write interface – triggers copy-on-write.
  void add(const void* data, uint64_t size) override;
  void addBufferFragment(BufferFragment& fragment) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void drain(uint64_t size) override;
  void addDrainTracker(std::function<void()> drain_tracker) override;
  void bindAccount(BufferMemoryAccountSharedPtr) override {}
  Reservation reserveForRead() override;
  ReservationSingleSlice reserveSingleSlice(uint64_t length, bool separate_slice = false) override;
  size_t addFragments(absl::Span<const absl::string_view> fragments) override;

  // Watermark interface: not supported for copy-on-write buffers used as lightweight views.
  void setWatermarks(uint64_t, uint32_t = 0) override {}
  uint64_t highWatermark() const override { return 0; }
  bool highWatermarkTriggered() const override { return false; }

private:
  void ensurePrivateBuffer();

protected:
  void commit(uint64_t length, absl::Span<RawSlice> slices,
              ReservationSlicesOwnerPtr slices_owner) override;

  SharedBufferPtr shared_buffer_;
  std::unique_ptr<Instance> private_buffer_;
};

std::unique_ptr<CopyOnWriteBuffer> createCopyOnWriteBuffer(std::unique_ptr<Instance> source_buffer);
std::vector<std::unique_ptr<CopyOnWriteBuffer>>
createSharedCopyOnWriteBuffers(std::unique_ptr<Instance> source_buffer, size_t count);

} // namespace Buffer
} // namespace Envoy
