#pragma once

#include "library/jni/import/jni_import.h"

namespace Envoy {
namespace JNI {

/**
 * JNI Environment variable wrapper.
 */
class Env {
public:
  static JNIEnv* get();

private:
  static thread_local JNIEnv* local_env_;
};

} // namespace JNI
} // namespace Envoy
