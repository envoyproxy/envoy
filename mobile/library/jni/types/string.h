#pragma once

#include "absl/strings/string_view.h"
#include "library/jni/import/jni_import.h"
#include "library/jni/types/env.h"

namespace Envoy {
namespace JNI {

/**
 * @brief A convenience wrapper that makes working with jstring easier and less error prone.
 * It takes care of managing freeing JNI resources when it's deallocated.
 */
class String {
public:
  String(const String&) = delete;

  /**
   * @brief Construct a new String object. Retrieves a pointer to an array of bytes representing
   * the string in modified UTF-8 encoding.
   */
  explicit String(jstring jni_string)
      : env_(Env::get()), jni_string_(jni_string),
        string_(env_->GetStringUTFChars(jni_string, nullptr)) {}

  ~String() {
    if (string_ != nullptr) {
      env_->ReleaseStringUTFChars(jni_string_, string_);
    }
  }

  /**
   * @brief Returns a string that represents the underlying array of bytes.
   */
  std::string get() {
    if (string_ != nullptr) {
      return std::string(string_);
    }
    return "nullptr";
  }
  void operator=(const String&) = delete;

private:
  JNIEnv* env_;
  jstring jni_string_;
  const char* string_;
};

} // namespace JNI
} // namespace Envoy
