#pragma once

#include "source/common/common/inline_map.h"

namespace Envoy {

/**
 * Registry class to get inline map descriptor singleton based on the scope. If cross-modules inline
 * map descriptor is necessary, this class should be used.
 *
 * There are only two allowed ways to update or modify the registry:
 * 1. Add inline map descriptors or inline keys to the registry before the main() function is called
 *    by the REGISTER_INLINE_MAP_DESCRIPTOR or REGISTER_INLINE_MAP_KEY macro.
 * 2. Add inline map descriptors or inline keys in the finalize() function.
 *
 * The finalize() function will be called once when the first server is initialized. This is safe
 * even there are multiple main threads or servers.
 */
class InlineMapRegistry {
public:
  /**
   * Helper template class to initialize descriptor based on the scope explicitly.
   */
  template <class Scope> class InlineMapDescriptorRegister {
  public:
    InlineMapDescriptorRegister();
  };

  /**
   * Helper template class to add inline key to the descriptor based on the scope.
   */
  template <class Scope> class InlineMapKeyRegister {
  public:
    InlineMapKeyRegister(absl::string_view);
  };

  using Descriptor = InlineMapDescriptor<std::string>;
  using Handle = Descriptor::Handle;

  /**
   * All registered descriptors. This is a map from scope name to the descriptor pointer.
   */
  using Descriptors = absl::flat_hash_map<std::string, Descriptor*>;

  struct ScopeInlineKeys {
    std::string name;
    absl::flat_hash_set<std::string> keys;
  };

  /**
   * Scope inline keys vector type. This is used to finalize the registry and add inline keys to
   * the descriptor based on the scope name.
   */
  using ScopeInlineKeysVector = std::vector<ScopeInlineKeys>;

private:
  template <class Scope> friend class InlineMapDescriptorRegister;
  template <class Scope> friend class InlineMapKeyRegister;

  /**
   * Default constructor. This is private because the registry should be a singleton that is only
   * created once and get by the InlineMapRegistry::get() function.
   */
  InlineMapRegistry() = default;

  /**
   * Create the inline map descriptor based on the scope name and insert it into the registry.
   */
  Descriptor* createDescriptor(absl::string_view scope_name);

  /**
   * Get or create the inline map descriptor singleton based on the scope type and insert
   * it into the registry.
   */
  template <class Scope> static Descriptor& getOrCreateDescriptor() {
    // The function level static variable will be initialized only once and is thread safe.
    // This is same with MUTABLE_CONSTRUCT_ON_FIRST_USE but has complex initialization.
    static Descriptor* initialize_once_descriptor = get().createDescriptor(Scope::name());
    return *initialize_once_descriptor;
  }

public:
  /**
   * Get the inline map registry singleton.
   */
  static InlineMapRegistry& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(InlineMapRegistry); }

  /**
   * Get the inline handle by the key name.
   */
  template <class Scope> absl::optional<Handle> static getHandleByKey(absl::string_view key) {
    return getOrCreateDescriptor<Scope>().getHandleByKey(key);
  }

  /**
   * Create inline map based on the scope and value type.
   */
  template <class Scope, class Value> static auto createInlineMap() {
    return InlineMap<std::string, Value>::create(getOrCreateDescriptor<Scope>());
  }

  /**
   * Get the registered descriptors. This is used to get all registered descriptors that are
   * managed by this registry.
   */
  const Descriptors& descriptors() const;

  /**
   * Finalize the registry and all managed descriptors by this registry. This only be called once
   * when the server is initialized. This is safe even there are multiple main threads or servers.
   */
  void finalize(const ScopeInlineKeysVector& scope_inline_keys_vector = {});

private:
  Descriptors descriptors_;
  std::once_flag once_flag_;
  bool finalized_{};
};

template <class Scope>
InlineMapRegistry::InlineMapDescriptorRegister<Scope>::InlineMapDescriptorRegister() {
  InlineMapRegistry::getOrCreateDescriptor<Scope>();
}

template <class Scope>
InlineMapRegistry::InlineMapKeyRegister<Scope>::InlineMapKeyRegister(absl::string_view key) {
  InlineMapRegistry::getOrCreateDescriptor<Scope>().addInlineKey(key);
}

/**
 * Helper macro to register inline map descriptor based on the scope explicitly. If cross-modules
 * inline map descriptor is necessary and InlineMapRegistry is used, then this macro should be used.
 */
#define REGISTER_INLINE_MAP_DESCRIPTOR(Scope)                                                      \
  static const Envoy::InlineMapRegistry::InlineMapDescriptorRegister<                              \
      Scope>##InlineMapDescriptorRegister;

/**
 * Helper macro to add inline key to the descriptor based on the scope. If cross-modules inline map
 * descriptor is necessary and InlineMapRegistry is used, then this macro should be used.
 */
#define REGISTER_INLINE_MAP_KEY(Scope, Key)                                                        \
  static const Envoy::InlineMapRegistry::InlineMapKeyRegister<Scope> Key##InlineMapKeyRegister(Key);

/**
 * Helper macro to get inline handle by the key name. Note this macro should be used in the function
 * that is called to get the inline handle. And please ensure the key is registered before the
 * function is called.
 */
#define INLINE_HANDLE_BY_KEY_ON_FIRST_USE(Scope, Key)                                              \
  do {                                                                                             \
    static auto handle = Envoy::InlineMapRegistry::getHandleByKey<Scope>(Key);                     \
    ASSERT(handle.has_value(), fmt::format("Cannot get inline handle by key {}", Key));            \
    return handle.value();                                                                         \
  } while (0)

} // namespace Envoy
