#include "library/jni/android_network_utility.h"

#include "library/common/bridge//utility.h"
#include "library/jni/jni_utility.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace JNI {

namespace {
// Helper functions call into AndroidNetworkLibrary, but they are not platform dependent
// because AndroidNetworkLibray can be called in non-Android platform with mock interfaces.

/**
 * The list of certificate verification results returned from Java side to the C++ side.
 * A Java counterpart lives in org.chromium.net.CertVerifyStatusAndroid.java.
 */
enum class CertVerifyStatus : int {
  // Certificate is trusted.
  Ok = 0,
  // Certificate verification could not be conducted.
  Failed = -1,
  // Certificate is not trusted due to non-trusted root of the certificate chain.
  NoTrustedRoot = -2,
  // Certificate is not trusted because it has expired.
  Expired = -3,
  // Certificate is not trusted because it is not valid yet.
  NotYetValid = -4,
  // Certificate is not trusted because it could not be parsed.
  UnableToParse = -5,
  // Certificate is not trusted because it has an extendedKeyUsage field, but its value is not
  // correct for a web server.
  IncorrectKeyUsage = -6,
};

bool jvmCertIsIssuedByKnownRoot(JniHelper& jni_helper, jobject result) {
  jclass jcls_AndroidCertVerifyResult =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult");
  jmethodID jmid_isIssuedByKnownRoot =
      jni_helper.getMethodIdFromCache(jcls_AndroidCertVerifyResult, "isIssuedByKnownRoot", "()Z");
  ASSERT(jmid_isIssuedByKnownRoot);
  bool is_issued_by_known_root = jni_helper.callBooleanMethod(result, jmid_isIssuedByKnownRoot);
  return is_issued_by_known_root;
}

CertVerifyStatus jvmCertGetStatus(JniHelper& jni_helper, jobject j_result) {
  jclass jcls_AndroidCertVerifyResult =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult");
  jmethodID jmid_getStatus =
      jni_helper.getMethodIdFromCache(jcls_AndroidCertVerifyResult, "getStatus", "()I");
  ASSERT(jmid_getStatus);
  CertVerifyStatus result =
      static_cast<CertVerifyStatus>(jni_helper.callIntMethod(j_result, jmid_getStatus));
  return result;
}

LocalRefUniquePtr<jobjectArray> jvmCertGetCertificateChainEncoded(JniHelper& jni_helper,
                                                                  jobject result) {
  jclass jcls_AndroidCertVerifyResult =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult");
  jmethodID jmid_getCertificateChainEncoded = jni_helper.getMethodIdFromCache(
      jcls_AndroidCertVerifyResult, "getCertificateChainEncoded", "()[[B");
  LocalRefUniquePtr<jobjectArray> certificate_chain =
      jni_helper.callObjectMethod<jobjectArray>(result, jmid_getCertificateChainEncoded);
  return certificate_chain;
}

static void extractCertVerifyResult(JniHelper& jni_helper, jobject result, CertVerifyStatus* status,
                                    bool* is_issued_by_known_root,
                                    std::vector<std::string>* verified_chain) {
  *status = jvmCertGetStatus(jni_helper, result);
  if (*status == CertVerifyStatus::Ok) {
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
                                   CertVerifyStatus* status, bool* is_issued_by_known_root,
                                   std::vector<std::string>* verified_chain) {
  JniHelper jni_helper(JniHelper::getThreadLocalEnv());
  LocalRefUniquePtr<jobject> result =
      callJvmVerifyX509CertChain(jni_helper, cert_chain, auth_type, hostname);
  if (jni_helper.exceptionCheck()) {
    *status = CertVerifyStatus::NotYetValid;
    jni_helper.exceptionCleared();
  } else {
    extractCertVerifyResult(jni_helper, result.get(), status, is_issued_by_known_root,
                            verified_chain);
    if (jni_helper.exceptionCheck()) {
      *status = CertVerifyStatus::Failed;
      jni_helper.exceptionCleared();
    }
  }
}

} // namespace

// `auth_type` and `host` are expected to be UTF-8 encoded.
LocalRefUniquePtr<jobject> callJvmVerifyX509CertChain(JniHelper& jni_helper,
                                                      const std::vector<std::string>& cert_chain,
                                                      std::string auth_type,
                                                      absl::string_view hostname) {
  jclass jcls_AndroidNetworkLibrary =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary");
  jmethodID jmid_verifyServerCertificates = jni_helper.getStaticMethodIdFromCache(
      jcls_AndroidNetworkLibrary, "verifyServerCertificates",
      "([[B[B[B)Lio/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult;");
  LocalRefUniquePtr<jobjectArray> chain_byte_array =
      vectorStringToJavaArrayOfByteArray(jni_helper, cert_chain);
  LocalRefUniquePtr<jbyteArray> auth_string = stringToJavaByteArray(jni_helper, auth_type);
  LocalRefUniquePtr<jbyteArray> host_string = byteArrayToJavaByteArray(
      jni_helper, reinterpret_cast<const uint8_t*>(hostname.data()), hostname.length());
  LocalRefUniquePtr<jobject> result = jni_helper.callStaticObjectMethod(
      jcls_AndroidNetworkLibrary, jmid_verifyServerCertificates, chain_byte_array.get(),
      auth_string.get(), host_string.get());
  return result;
}

envoy_cert_validation_result verifyX509CertChain(const std::vector<std::string>& certs,
                                                 absl::string_view hostname) {
  CertVerifyStatus result;
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
  case CertVerifyStatus::Ok:
    return {ENVOY_SUCCESS, 0, nullptr};
  case CertVerifyStatus::Expired: {
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_EXPIRED,
            "AndroidNetworkLibrary_verifyServerCertificates failed: expired cert."};
  }
  case CertVerifyStatus::NoTrustedRoot:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: no trusted root."};
  case CertVerifyStatus::UnableToParse:
    return {ENVOY_FAILURE, SSL_AD_BAD_CERTIFICATE,
            "AndroidNetworkLibrary_verifyServerCertificates failed: unable to parse cert."};
  case CertVerifyStatus::IncorrectKeyUsage:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: incorrect key usage."};
  case CertVerifyStatus::Failed:
    return {
        ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
        "AndroidNetworkLibrary_verifyServerCertificates failed: validation couldn't be conducted."};
  case CertVerifyStatus::NotYetValid:
    return {ENVOY_FAILURE, SSL_AD_CERTIFICATE_UNKNOWN,
            "AndroidNetworkLibrary_verifyServerCertificates failed: not yet valid."};
  default:
    PANIC_DUE_TO_CORRUPT_ENUM
  }
}

void jvmDetachThread() { JniHelper::detachCurrentThread(); }

} // namespace JNI
} // namespace Envoy
