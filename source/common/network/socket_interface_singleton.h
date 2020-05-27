#pragma once

#include "envoy/network/socket.h"

#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Network {

SocketInterface* createDefaultSocketInterface();

template <class T> class CustomScopedSingleton {
public:
  CustomScopedSingleton(std::unique_ptr<T>&& instance) {
    if (InjectableSingleton<T>::getExisting() == nullptr) {
      instance_ = std::move(instance);
      InjectableSingleton<T>::initialize(instance_.get());
    }
  }
  static T& get() {
    if (InjectableSingleton<T>::getExisting() == nullptr) {
      absl::call_once(CustomScopedSingleton<T>::create_once_, &CustomScopedSingleton<T>::Create);
    }
    return InjectableSingleton<T>::get();
  }

  ~CustomScopedSingleton() {
    if (default_ == nullptr) {
      InjectableSingleton<T>::clear();
    }
  }

protected:
  static void Create() {
    default_ = createDefaultSocketInterface();
    InjectableSingleton<T>::initialize(default_);
  }

  std::unique_ptr<T> instance_;
  static absl::once_flag create_once_;
  static T* default_;
};

template <class T> absl::once_flag CustomScopedSingleton<T>::create_once_;

template <class T> T* CustomScopedSingleton<T>::default_ = nullptr;

using SocketInterfaceSingleton = CustomScopedSingleton<SocketInterface>;

} // namespace Network
} // namespace Envoy