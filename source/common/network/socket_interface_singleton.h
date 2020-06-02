#pragma once

#include "envoy/network/socket.h"

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Network {

SocketInterface* createDefaultSocketInterface();

template <class T> class CustomInjectableSingleton {
public:
  static void initialize(std::unique_ptr<T>&& instance) {
    absl::call_once(CustomInjectableSingleton<T>::create_once_, &CustomInjectableSingleton<T>::init,
                    std::move(instance));
  }

  static T& get() {
    absl::call_once(CustomInjectableSingleton<T>::create_once_,
                    &CustomInjectableSingleton<T>::create);
    return InjectableSingleton<T>::get();
  }

protected:
  static void create() {
    static T* default_ = createDefaultSocketInterface();
    InjectableSingleton<T>::initialize(default_);
  }

  static void init(std::unique_ptr<T>&& instance) {
    instance_ = std::move(instance);
    InjectableSingleton<T>::initialize(instance_.get());
  }

  static std::unique_ptr<T> instance_;
  static absl::once_flag create_once_;
};

template <class T> absl::once_flag CustomInjectableSingleton<T>::create_once_;

template <class T> std::unique_ptr<T> CustomInjectableSingleton<T>::instance_;

using SocketInterfaceSingleton = CustomInjectableSingleton<SocketInterface>;

} // namespace Network
} // namespace Envoy