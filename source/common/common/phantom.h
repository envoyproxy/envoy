#pragma once

#include <initializer_list>
#include <type_traits>

namespace Envoy {

/**
 * A phantom type provides additional type safety to types that are otherwise
 * interchangeable. For instance, two uint32_t might have different semantic
 * meaning, so expressing them with a phantom type provides some compile time
 * guarantees that they won't be used interchangeably.
 *
 * Since a phantom type subclasses the inner type, they can be used wherever
 * the inner type is required. To ensure that the phantom type is not implictly
 * created one of the ctors of the inner type, all ctors are marked explicit.
 */
template <class I, class M> struct Phantom : I {
  // This allows invoking any of the ctors of the inner class, based on the inferred type
  // arguments to the Phantom ctor. This has consequences for how ctors are resolved
  // compared to the inner type, see the below comment.
  template <class... S> explicit Phantom(S&&... v) : I(std::forward<S>(v)...) {}

  // This ensures that we're able to use an initializer list to create Phantoms in a ergonomic
  // way, eg.
  //
  // Phantom<std::vector<uint32_t>, struct Foo> p{1u, 2u, 3u};
  //
  // *NOTE*: This works similar to how it does for standard types, but because of the variadic
  // ctor above, when the initializer list needs to be implicitly converted, another ctor might
  // be chosen instead, eg.
  //
  // this calls the iniitalizer list ctor
  // Phantom<std::vector<uint32_t>, struct Foo> p{1u, 2u}; // call initializer list ctor
  //
  // calls the (int&&, int&&) phantom ctor, which  ends up resolving to the
  // (size_t, const uint32_t&) vector ctor.
  // Phantom<std::vector<uint32_t>, struct Foo> p{1, 2};
  //
  // This differs from constructing a std::vector<unit32_t> directly, where both would invoke the
  // initializter list ctor.
  //
  // The std::enable_if_t is necessary to disable this when the innter class doesn't support
  // initializer list initialization.
  template <class S,
            std::enable_if_t<std::is_constructible<I, std::initializer_list<S>>::value, int> = 0>
  Phantom(std::initializer_list<S> init) : I(init) {}
};

} // namespace Envoy
