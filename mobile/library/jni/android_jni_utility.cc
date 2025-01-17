#include "library/jni/android_jni_utility.h"

#include "source/common/common/assert.h"

#if defined(__ANDROID_API__)
#include "library/common/bridge/utility.h"
#include "library/jni/jni_utility.h"
#include "library/jni/jni_helper.h"
#endif

namespace Envoy {
namespace JNI {

bool isCleartextPermitted(absl::string_view hostname) {
#if defined(__ANDROID_API__)
  JniHelper jni_helper(JniHelper::getThreadLocalEnv());
  LocalRefUniquePtr<jstring> java_host = cppStringToJavaString(jni_helper, std::string(hostname));
  jclass java_android_network_library_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary");
  jmethodID java_is_cleartext_traffic_permitted_method_id = jni_helper.getStaticMethodIdFromCache(
      java_android_network_library_class, "isCleartextTrafficPermitted", "(Ljava/lang/String;)Z");
  jboolean result = jni_helper.callStaticBooleanMethod(
      java_android_network_library_class, java_is_cleartext_traffic_permitted_method_id,
      java_host.get());
  return result == JNI_TRUE;
#else
  UNREFERENCED_PARAMETER(hostname);
  return true;
#endif
}

void tagSocket(int ifd, int uid, int tag) {
#if defined(__ANDROID_API__)
  JniHelper jni_helper(JniHelper::getThreadLocalEnv());
  jclass java_android_network_library_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary");
  jmethodID java_tag_socket_method_id = jni_helper.getStaticMethodIdFromCache(
      java_android_network_library_class, "tagSocket", "(III)V");
  jni_helper.callStaticVoidMethod(java_android_network_library_class, java_tag_socket_method_id,
                                  ifd, uid, tag);
#else
  UNREFERENCED_PARAMETER(ifd);
  UNREFERENCED_PARAMETER(uid);
  UNREFERENCED_PARAMETER(tag);
#endif
}

} // namespace JNI
} // namespace Envoy
