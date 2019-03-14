#include "common/common/assert.h"
#include "envoy/buffer/buffer.h"
#include "quiche/quic/core/quic_buffer_allocator.h"

#include <memory>
#include <iostream>

namespace quic {

class QuicMemSliceImpl {
public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl() = default;

  // Constructs a QuicMemSliceImpl by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length)
      : raw_slice_(allocator->New(length), [allocator, length](auto slice){
        ASSERT(slice != nullptr);
        consumeSlice(slice, length, allocator, nullptr);
      }),length_(length),
        parent_(nullptr), allocator_(allocator) {
          std::cerr << "construct mem slice " << this << " from allocator " << allocator <<  " slice =" << reinterpret_cast<size_t>(raw_slice_.get()) << "\n";
        }

  // Constructs a QuicMemSliceImpl from a RawSlice, |parent| points to the owner
  // of |raw_slice|.
  QuicMemSliceImpl(const Envoy::Buffer::RawSlice& raw_slice,
                   std::shared_ptr<Envoy::Buffer::Instance> parent)
      : raw_slice_(static_cast<char*>(raw_slice.mem_), [parent, len=raw_slice.len_](auto slice){ consumeSlice(slice, len, nullptr, std::move(parent));}),

        length_(raw_slice.len_), parent_(parent), allocator_(nullptr) {
            std::cerr << "construct mem slice " << this << " from RawSlice " << reinterpret_cast<size_t>(raw_slice_.get()) << "\n";
        }


  QuicMemSliceImpl(const QuicMemSliceImpl& other) = default;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = default;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other) : raw_slice_(std::move(other.raw_slice_)), length_(other.length_), parent_(std::move(other.parent_)), allocator_(other.allocator_) {
    other.Reset();
    std::cerr << "construct mem slice " << this << " by move from " << &other << " raw_slice = " << raw_slice_.get() << "\n";
  }

  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other) {
    std::cerr << "move assignment to mem slice " << this << " from " << &other << " raw_slice_ = " << reinterpret_cast<size_t>(other.raw_slice_.get()) << "\n";
    if (this != &other) {
      raw_slice_ = std::move(other.raw_slice_);
      length_ = other.length_;
      parent_ = std::move(other.parent_);
      allocator_ = other.allocator_;
      other.Reset();
    }
    std::cerr << "allocator_ = " << allocator_ << "\n";
    return *this;
  }

  ~QuicMemSliceImpl() {
    std::cerr << "destruct mem slice " << this << " raw slice = " << reinterpret_cast<size_t>(raw_slice_.get()) << " allocator_ = " << allocator_ << "\n";
      if (allocator_ != nullptr) {
        std::cerr << "~QuicMemSliceImpl test allocator " << allocator_ << "\n";
        allocator_->Delete(allocator_->New(10)); }

  }

  // Release the underlying reference. Further access the memory will result in
  // undefined behavior.
  void Reset() {
    // Release |raw_slice| before releasing |parent_|.
    raw_slice_ = nullptr;
    length_ = 0;
    parent_ = nullptr;
    allocator_ = nullptr;
  }

  // Returns a char pointer to underlying data buffer.
  const char* data() const {
    return static_cast<const char*>(raw_slice_.get());
  }

  // Returns the length of underlying data buffer.
  size_t length() const { return length_; }

  bool empty() const { return length_ == 0; }

private:
  static void consumeSlice(char* slice, size_t length, QuicBufferAllocator* allocator, std::shared_ptr<Envoy::Buffer::Instance> parent) {
    if (parent == nullptr) {
      ASSERT(allocator != nullptr);
        std::cerr << "=========== consumeSlice: test allocator " << allocator << "\n";
      allocator->Delete(allocator->New(10));
        std::cerr <<  "Delete slice " << reinterpret_cast<size_t>(slice);
      allocator->Delete(slice);
      return;
    }
    parent->drain(length);
  }

  std::shared_ptr<char[]> raw_slice_;
  size_t length_;
  // Either |parent_| or |allocator_| should be nullptr.
  std::shared_ptr<Envoy::Buffer::Instance> parent_;
  QuicBufferAllocator* allocator_;
};

}  // namespace quic
