#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "envoy/common/optref.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {

/**
 * A utility wrapper that ensures a protocol buffer message is allocated on a
 * built-in protocol buffer arena, linking their lifetimes.
 *
 * This is useful for ensuring that the message and all of its dynamically
 * allocated sub-messages (created via mutable_foo() accessors) reside on the
 * same arena, optimizing memory allocation and deallocation.
 */
template <typename ProtoT> class ArenaWrappedProto {
public:
  // Constructor: Creates the arena and allocates the proto on it.
  // Perfect forwards any arguments to the ProtoT constructor.
  template <typename... Args>
  explicit ArenaWrappedProto(Args&&... args)
      : arena_(std::make_unique<google::protobuf::Arena>()),
        proto_(google::protobuf::Arena::Create<ProtoT>(arena_.get(), std::forward<Args>(args)...)) {
  }

  // Disallow copy (copying the arena is not supported/safe).
  ArenaWrappedProto(const ArenaWrappedProto&) = delete;
  ArenaWrappedProto& operator=(const ArenaWrappedProto&) = delete;

  // Allow move.
  ArenaWrappedProto(ArenaWrappedProto&& other) noexcept
      : arena_(std::move(other.arena_)), proto_(std::exchange(other.proto_, nullptr)) {}

  ArenaWrappedProto& operator=(ArenaWrappedProto&& other) noexcept {
    if (this != &other) {
      // The current arena_ and its contents are automatically deallocated.
      arena_ = std::move(other.arena_);
      proto_ = std::exchange(other.proto_, nullptr);
    }
    return *this;
  }

  ~ArenaWrappedProto() = default; // Destructor of arena_ frees proto_ automatically.

  // Allow construction from nullptr.
  ArenaWrappedProto(std::nullptr_t) : arena_(nullptr), proto_(nullptr) {}

  // Conversion constructor from std::unique_ptr.
  // This is implicit to facilitate migration from std::unique_ptr to ArenaWrappedProto.
  ArenaWrappedProto(std::unique_ptr<ProtoT>&& heap_proto) {
    if (heap_proto) {
      arena_ = std::make_unique<google::protobuf::Arena>();
      proto_ = google::protobuf::Arena::Create<ProtoT>(arena_.get());
      proto_->Swap(heap_proto.get());
    } else {
      arena_ = nullptr;
      proto_ = nullptr;
    }
  }

  // Boolean conversion (e.g., if (wrapper))
  explicit operator bool() const { return proto_ != nullptr; }

  // Comparison with nullptr.
  bool operator==(std::nullptr_t) const { return proto_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return proto_ != nullptr; }

  // --- Transparent Accessors ---

  // Transparent member access (e.g., wrapper->field())
  ProtoT* operator->() { return proto_; }
  const ProtoT* operator->() const { return proto_; }

  // Dereference operators (e.g., *wrapper)
  ProtoT& operator*() { return *proto_; }
  const ProtoT& operator*() const { return *proto_; }

  // Implicit conversions for passing to functions expecting the raw proto
  // reference
  operator ProtoT&() { return *proto_; }
  operator const ProtoT&() const { return *proto_; }

  // Implicit conversions for passing to functions expecting OptRef.
  operator OptRef<ProtoT>() { return *proto_; }
  operator const OptRef<const ProtoT>() const { return *proto_; }

  // Explicit accessors
  ProtoT* get() { return proto_; }
  const ProtoT* get() const { return proto_; }
  google::protobuf::Arena* arena() { return arena_.get(); }

private:
  std::unique_ptr<google::protobuf::Arena> arena_;
  ProtoT* proto_; // Points to memory owned by arena_
};

} // namespace Envoy
