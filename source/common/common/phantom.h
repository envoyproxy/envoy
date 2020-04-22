#pragma once

#include <utility>

namespace Envoy {

// A phantom type allows wrapping a common type with additional type information in order to allow
// additional compile time safety when passing it around.
template <class InnerT, class TagT> struct Phantom {
  Phantom() = default;
  explicit Phantom(const InnerT& t) : val_(t) {}
  explicit Phantom(InnerT&& t) : val_(std::move(t)) {}

  InnerT& get() { return val_; }
  const InnerT& get() const { return val_; };

  bool operator==(const Phantom<InnerT, TagT>& other) const { return val_ == other.val_; }

private:
  InnerT val_;
};

} // namespace Envoy
