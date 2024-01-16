#include "library/common/jni/android_jni_utility.h"

#include "source/common/common/assert.h"

#if defined(__ANDROID_API__)
#include "library/common/data/utility.h"
#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#endif

namespace Envoy {
namespace JNI {

bool isCleartextPermitted(absl::string_view hostname) {
#if defined(__ANDROID_API__)
  envoy_data host = Envoy::Data::Utility::copyToBridgeData(hostname);
  JniHelper jni_helper(getEnv());
  LocalRefUniquePtr<jstring> java_host = envoyDataToJavaString(jni_helper, host);
  LocalRefUniquePtr<jclass> jcls_AndroidNetworkLibrary =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_isCleartextTrafficPermitted = jni_helper.getStaticMethodId(
      jcls_AndroidNetworkLibrary.get(), "isCleartextTrafficPermitted", "(Ljava/lang/String;)Z");
  jboolean result = jni_helper.callStaticBooleanMethod(
      jcls_AndroidNetworkLibrary.get(), jmid_isCleartextTrafficPermitted, java_host.get());
  release_envoy_data(host);
  return result == JNI_TRUE;
#else
  UNREFERENCED_PARAMETER(hostname);
  return true;
#endif
}

void tagSocket(int ifd, int uid, int tag) {
#if defined(__ANDROID_API__)
  JniHelper jni_helper(getEnv());
  LocalRefUniquePtr<jclass> jcls_AndroidNetworkLibrary =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_tagSocket =
      jni_helper.getStaticMethodId(jcls_AndroidNetworkLibrary.get(), "tagSocket", "(III)V");
  jni_helper.callStaticVoidMethod(jcls_AndroidNetworkLibrary.get(), jmid_tagSocket, ifd, uid, tag);
#else
  UNREFERENCED_PARAMETER(ifd);
  UNREFERENCED_PARAMETER(uid);
  UNREFERENCED_PARAMETER(tag);
#endif
}

} // namespace JNI
} // namespace Envoy
