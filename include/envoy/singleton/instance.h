#pragma once

namespace Envoy {
namespace Singleton {

/**
 * All singletons must derive from this type.
 */
class Instance {
public:
  virtual ~Instance() {}
};

typedef std::shared_ptr<Instance> InstanceSharedPtr;

/**
 * Macro used to statically register singletons managed by the singleton manager
 * defined in envoy/singleton/manager.h. After the NAME has been registered use the
 * SINGLETON_MANAGER_REGISTERED_NAME macro to access the name registered with the
 * singleton manager.
 */
#define SINGLETON_MANAGER_REGISTRATION(NAME)                                                       \
  static constexpr char NAME##_singleton_name[] = #NAME "_singleton";                              \
  static Registry::RegisterFactory<Singleton::RegistrationImpl<NAME##_singleton_name>,             \
                                   Singleton::Registration>                                        \
      NAME##_singleton_registered_;

#define SINGLETON_MANAGER_REGISTERED_NAME(NAME) NAME##_singleton_name

} // namespace Singleton
} // namespace Envoy
