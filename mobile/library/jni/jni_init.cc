#include "library/jni/jni_init.h"

#include "library/jni/jni_helper.h"
#include "library/jni/jni_utility.h"

namespace Envoy {
namespace JNI {

void initialize(JavaVM* jvm) {
  JniHelper::initialize(jvm);
  JniUtility::initCache();
  JniHelper::addToCache(
      "io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary",
      /* methods= */ {},
      /* static_methods= */
      {
          {"isCleartextTrafficPermitted", "(Ljava/lang/String;)Z"},
          {"tagSocket", "(III)V"},
          {"verifyServerCertificates",
           "([[B[B[B)Lio/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult;"},
          {"addTestRootCertificate", "([B)V"},
          {"clearTestRootCertificates", "()V"},

      },
      /* fields= */ {}, /* static_fields= */ {});
  JniHelper::addToCache("io/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult",
                        /* methods= */
                        {
                            {"isIssuedByKnownRoot", "()Z"},
                            {"getStatus", "()I"},
                            {"getCertificateChainEncoded", "()[[B"},
                        },
                        /* static_methods= */ {},
                        /* fields= */ {}, /* static_fields= */ {});
  JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyOnEngineRunning",
                        /* methods= */
                        {
                            {"invokeOnEngineRunning", "()Ljava/lang/Object;"},
                        },
                        /* static_methods= */ {},
                        /* fields= */ {}, /* static_fields= */ {});
  JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyLogger",
                        /* methods= */
                        {
                            {"log", "(ILjava/lang/String;)V"},
                        },
                        /* static_methods= */ {},
                        /* fields= */ {}, /* static_fields= */ {});
  JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyEventTracker",
                        /* methods= */
                        {
                            {"track", "(Ljava/util/Map;)V"},
                        },
                        /* static_methods= */ {},
                        /* fields= */ {}, /* static_fields= */ {});
  JniHelper::addToCache(
      "io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks",
      /* methods= */
      {
          {"onHeaders",
           "(Ljava/util/Map;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onData",
           "(Ljava/nio/ByteBuffer;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onTrailers",
           "(Ljava/util/Map;Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onComplete", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/envoyproxy/"
                         "envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onError",
           "(ILjava/lang/String;ILio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/"
           "envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onCancel", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/envoyproxy/"
                       "envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onSendWindowAvailable", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
      },
      /* static_methods= */ {},
      /* fields= */ {}, /* static_fields= */ {});
}

void finalize() { JniHelper::finalize(); }

} // namespace JNI
} // namespace Envoy
