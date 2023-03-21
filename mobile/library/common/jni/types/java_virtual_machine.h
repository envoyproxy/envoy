#pragma once

#include "library/common/jni/import/jni_import.h"

namespace Envoy {
namespace JNI {

/**
 * @brief A convenience wrapper for JNI JavaVM type.
 *
 */
class JavaVirtualMachine {
public:
  /**
   * @brief Initializes virtual machine with JavaVM type. It should be called only once.
   */
  static void initialize(JavaVM* jvm);
  static JavaVM* getJavaVM();
  static jint getJNIVersion();
  static void detachCurrentThread();

private:
  static JavaVM* jvm_;
};

} // namespace JNI
} // namespace Envoy
