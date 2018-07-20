#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadata {
 public:
  virtual ~DynamicMetadata() {};

  // May only be called once for a particular |data_name|
  template <typename T>
  void setData(
      absl::string_view data_name,
      std::unique_ptr<T>&& data) {
    setDataGeneric(data_name, Traits<T>::getTypeId(), static_cast<void*>(data.release()),
                   &Traits<T>::destructor);
  }

  template <typename T>
  const T& getData(absl::string_view data_name) {
    return *static_cast<T*>(getDataGeneric(data_name, Traits<T>::getTypeId()));
  }

 protected:
  virtual void setDataGeneric(
      absl::string_view data_name,
      size_t type_id,
      void* data,                       // Implementation must take ownership
      void (*destructor)(void *)) PURE;

  virtual void* getDataGeneric(
      absl::string_view data_name,
      size_t type_id) PURE;

 private:
  // TODO(rdsmith): Is it ok to default this to presumably zero this way?
  static size_t type_id_index_;

  template <typename T>
  class Traits {
   public:
    static size_t getTypeId() {
      static const size_t type_id = ++type_id_index_;
      return type_id;
    }
    static void destructor(void *ptr) {
      delete static_cast<T*>(ptr);
    }
  };
};

} // namespace RequestInfo
} // namespace Envoy
