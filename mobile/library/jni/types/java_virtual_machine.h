#pragma once

#include "library/jni/import/jni_import.h"

namespace Envoy {
namespace JNI {

/**
 * @brief A convenience wrapper for JNI JavaVM type.
 *
 */
class JavaVirtualMachine {
public:
  /**
   * @brief Initializes virtual machine. It should be called only once.
   *
   * @param jvm A pointer to running JavaVM instance.
   * @return jint JNI_OK if the operation was successful, error code information otherwise.
   */
  static jint initialize(JavaVM* jvm);
  static JavaVM* getJavaVM();
  static jint getJNIVersion();
  static void detachCurrentThread();

private:
  static JavaVM* jvm_;
};

} // namespace JNI
} // namespace Envoy
