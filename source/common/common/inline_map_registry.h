#include "source/common/common/inline_map.h"

namespace Envoy {

/**
 * Registry class to get inline map descriptor singleton based on the scope. If cross-modules inline
 * map descriptor is necessary, this class should be used.
 */
class InlineMapRegistry {
public:
  using InlineKey = InlineMapDescriptor<std::string>::InlineKey;
  using ManagedDescriptors = absl::flat_hash_map<std::string, InlineMapDescriptor<std::string>*>;

  /**
   * Get the inline map registry singleton based on the scope.
   */
  template <class Scope>
  static InlineMapDescriptor<std::string>::InlineKey addInlineKey(absl::string_view key) {
    return mutableDescriptor<Scope>().addInlineKey(key);
  }

  /**
   * Get the added inline key by the key name.
   */
  template <class Scope>
  static absl::optional<InlineMapDescriptor<std::string>::InlineKey>
  getInlineKey(absl::string_view key) {
    return mutableDescriptor<Scope>().getInlineKey(key);
  }

  /**
   * Create inline map based on the scope.
   */
  template <class Scope, class Value> static auto createInlineMap() {
    return InlineMap<std::string, Value>::create(mutableDescriptor<Scope>());
  }

  /**
   * Get all descriptors managed by this registry. This this helpful to finalize all descriptors and
   * print the debug info of inline map registry.
   */
  static const ManagedDescriptors& descriptors() { return mutableDescriptors(); }

  /**
   * Finalize all descriptors managed by this registry. This should be called after all inline keys
   * are added.
   */
  static void finalize() {
    for (auto& registry : mutableDescriptors()) {
      registry.second->finalize();
    }
  }

private:
  template <class Scope> static InlineMapDescriptor<std::string>& mutableDescriptor() {
    static InlineMapDescriptor<std::string>* descriptor = []() {
      auto* inner = new InlineMapDescriptor<std::string>();
      auto result = InlineMapRegistry::mutableDescriptors().emplace(Scope::name(), inner);
      RELEASE_ASSERT(result.second,
                     absl::StrCat("Duplicate inline map registry for scope: ", Scope::name()));
      return inner;
    }();
    return *descriptor;
  }

  static ManagedDescriptors& mutableDescriptors() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(ManagedDescriptors);
  }
};

// Macro to add inline key to the descriptor based on the scope. This will create a static variable
// with the name of the key and the scope.
#define REGISTER_INLINE_KEY(Scope, Name, Key)                                                      \
  static const auto Name = Envoy::InlineMapRegistry::addInlineKey<Scope>(Key);

} // namespace Envoy
