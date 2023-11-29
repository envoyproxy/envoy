#include "library/common/jni/android_network_utility.h"

#include "library/common/data/utility.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/jni/types/exception.h"
#include "library/common/jni/types/java_virtual_machine.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace JNI {

namespace {
// Helper functions call into AndroidNetworkLibrary, but they are not platform dependent
// because AndroidNetworkLibray can be called in non-Android platform with mock interfaces.

bool jvmCertIsIssuedByKnownRoot(JniHelper& jni_helper, jobject result) {
  LocalRefUniquePtr<jclass> jcls_AndroidCertVerifyResult =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_isIssuedByKnownRoot =
      jni_helper.getMethodId(jcls_AndroidCertVerifyResult.get(), "isIssuedByKnownRoot", "()Z");
  ASSERT(jmid_isIssuedByKnownRoot);
  bool is_issued_by_known_root = jni_helper.callBooleanMethod(result, jmid_isIssuedByKnownRoot);
  return is_issued_by_known_root;
}

envoy_cert_verify_status_t jvmCertGetStatus(JniHelper& jni_helper, jobject j_result) {
  LocalRefUniquePtr<jclass> jcls_AndroidCertVerifyResult =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_getStatus =
      jni_helper.getMethodId(jcls_AndroidCertVerifyResult.get(), "getStatus", "()I");
  ASSERT(jmid_getStatus);
  envoy_cert_verify_status_t result =
      static_cast<envoy_cert_verify_status_t>(jni_helper.callIntMethod(j_result, jmid_getStatus));
  return result;
}

LocalRefUniquePtr<jobjectArray> jvmCertGetCertificateChainEncoded(JniHelper& jni_helper,
                                                                  jobject result) {
  LocalRefUniquePtr<jclass> jcls_AndroidCertVerifyResult =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidCertVerifyResult");
  jmethodID jmid_getCertificateChainEncoded = jni_helper.getMethodId(
      jcls_AndroidCertVerifyResult.get(), "getCertificateChainEncoded", "()[[B");
  LocalRefUniquePtr<jobjectArray> certificate_chain =
      jni_helper.callObjectMethod<jobjectArray>(result, jmid_getCertificateChainEncoded);
  return certificate_chain;
}

static void extractCertVerifyResult(JniHelper& jni_helper, jobject result,
                                    envoy_cert_verify_status_t* status,
                                    bool* is_issued_by_known_root,
                                    std::vector<std::string>* verified_chain) {
  *status = jvmCertGetStatus(jni_helper, result);
  if (*status == CERT_VERIFY_STATUS_OK) {
    *is_issued_by_known_root = jvmCertIsIssuedByKnownRoot(jni_helper, result);
    LocalRefUniquePtr<jobjectArray> chain_byte_array =
        jvmCertGetCertificateChainEncoded(jni_helper, result);
    if (chain_byte_array != nullptr) {
      javaArrayOfByteArrayToStringVector(jni_helper, chain_byte_array.get(), verified_chain);
    }
  }
}

// `auth_type` and `host` are expected to be UTF-8 encoded.
static void jvmVerifyX509CertChain(const std::vector<std::string>& cert_chain,
                                   std::string auth_type, absl::string_view hostname,
                                   envoy_cert_verify_status_t* status,
                                   bool* is_issued_by_known_root,
                                   std::vector<std::string>* verified_chain) {
  JniHelper jni_helper(getEnv());
  LocalRefUniquePtr<jobject> result =
      callJvmVerifyX509CertChain(jni_helper, cert_chain, auth_type, hostname);
  if (Exception::checkAndClear()) {
    *status = CERT_VERIFY_STATUS_NOT_YET_VALID;
  } else {
    extractCertVerifyResult(jni_helper, result.get(), status, is_issued_by_known_root,
                            verified_chain);
    if (Exception::checkAndClear()) {
      *status = CERT_VERIFY_STATUS_FAILED;
    }
  }
}

} // namespace

// `auth_type` and `host` are expected to be UTF-8 encoded.
LocalRefUniquePtr<jobject> callJvmVerifyX509CertChain(Envoy::JNI::JniHelper& jni_helper,
                                                      const std::vector<std::string>& cert_chain,
                                                      std::string auth_type,
                                                      absl::string_view hostname) {
  jni_log("[Envoy]", "jvmVerifyX509CertChain");
  LocalRefUniquePtr<jclass> jcls_AndroidNetworkLibrary =
      findClass("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_verifyServerCertificates = jni_helper.getStaticMethodId(
      jcls_AndroidNetworkLibrary.get(), "verifyServerCertificates",
      "([[B[B[B)Lio/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult;");
  LocalRefUniquePtr<jobjectArray> chain_byte_array =
      vectorStringToJavaArrayOfByteArray(jni_helper, cert_chain);
  LocalRefUniquePtr<jbyteArray> auth_string = stringToJavaByteArray(jni_helper, auth_type);
  LocalRefUniquePtr<jbyteArray> host_string = byteArrayToJavaByteArray(
      jni_helper, reinterpret_cast<const uint8_t*>(hostname.data()), hostname.length());
  LocalRefUniquePtr<jobject> result = jni_helper.callStaticObjectMethod(
      jcls_AndroidNetworkLibrary.get(), jmid_verifyServerCertificates, chain_byte_array.get(),
      auth_string.get(), host_string.get());
  return result;
}

envoy_cert_validation_result verifyX509CertChain(const std::vector<std::string>& certs,
                                                 absl::string_view hostname) {
  jni_log("[Envoy]", "verifyX509CertChain");

  envoy_cert_verify_status_t result;
  bool is_issued_by_known_root;
  std::vector<std::string> verified_chain;
  std::vector<std::string> cert_chain;
  for (absl::string_view cert : certs) {
    cert_chain.push_back(std::string(cert));
  }

  // Android ignores the authType parameter to X509TrustManager.checkServerTrusted, so pass in "RSA"
  // as dummy value. See https://crbug.com/627154.
  jvmVerifyX509CertChain(cert_chain, "RSA", hostname, &result, &is_issued_by_known_root,
                         &verified_chain);
  switch (result) {
  case CERT_VERIFY_STATUS_OK:
    return {ENVOY_SUCCESS};
  case CERT_VERIFY_STATUS_EXPIRED: {
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_EXPIRED,
            "AndroidNetworkLibrary_verifyServerCertificates failed: expired cert."};
  }
  case CERT_VERIFY_STATUS_NO_TRUSTED_ROOT:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: no trusted root."};
  case CERT_VERIFY_STATUS_UNABLE_TO_PARSE:
    return {ENVOY_FAILURE, SSL_AD_BAD_CERTIFICATE,
            "AndroidNetworkLibrary_verifyServerCertificates failed: unable to parse cert."};
  case CERT_VERIFY_STATUS_INCORRECT_KEY_USAGE:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: incorrect key usage."};
  case CERT_VERIFY_STATUS_FAILED:
    return {
        ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
        "AndroidNetworkLibrary_verifyServerCertificates failed: validation couldn't be conducted."};
  case CERT_VERIFY_STATUS_NOT_YET_VALID:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: not yet valid."};
  default:
    PANIC_DUE_TO_CORRUPT_ENUM
  }
}

void jvmDetachThread() { JavaVirtualMachine::detachCurrentThread(); }

} // namespace JNI
} // namespace Envoy
