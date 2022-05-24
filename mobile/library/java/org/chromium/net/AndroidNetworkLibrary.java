package org.chromium.net;

import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;

import java.nio.charset.StandardCharsets;

/**
 * This class implements net utilities required by the net component.
 */
public final class AndroidNetworkLibrary {
  private static final String TAG = "AndroidNetworkLibrary";

  private static boolean mUseFakeCertificateVerification;

  /**
   * Whether a fake should be used in place of X509Util. This allows to easily test the JNI
   * call interaction in robolectric tests.
   *
   * @param useFakeCertificateVerification Whether FakeX509Util should be used or not.
   */
  public static void
  setFakeCertificateVerificationForTesting(boolean useFakeCertificateVerification) {
    mUseFakeCertificateVerification = useFakeCertificateVerification;
  }

  /**
   * Validate the server's certificate chain is trusted. Note that the caller
   * must still verify the name matches that of the leaf certificate.
   * This is called from native code.
   *
   * @param certChain The ASN.1 DER encoded bytes for certificates.
   * @param authType Bytes representing the UTF-8 encoding of the key exchange algorithm name (e.g.
   *     RSA).
   * @param host Bytes representing the UTF-8 encoding of the hostname of the server.
   * @return Android certificate verification result code.
   */
  public static AndroidCertVerifyResult
  verifyServerCertificates(byte[][] certChain, byte[] authTypeBytes, byte[] hostBytes) {
    String authType = new String(authTypeBytes, StandardCharsets.UTF_8);
    String host = new String(hostBytes, StandardCharsets.UTF_8);
    if (mUseFakeCertificateVerification) {
      return FakeX509Util.verifyServerCertificates(certChain, authType, host);
    }

    try {
      return X509Util.verifyServerCertificates(certChain, authType, host);
    } catch (KeyStoreException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    } catch (NoSuchAlgorithmException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    } catch (IllegalArgumentException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    }
  }

  /**
   * Adds a test root certificate to the local trust store.
   * This is called from native code.
   *
   * @param rootCert DER encoded bytes of the certificate.
   */
  public static void addTestRootCertificate(byte[] rootCert)
      throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
    if (mUseFakeCertificateVerification) {
      FakeX509Util.addTestRootCertificate(rootCert);
    } else {
      X509Util.addTestRootCertificate(rootCert);
    }
  }

  /**
   * Removes all test root certificates added by |addTestRootCertificate| calls from the local
   * trust store.
   * This is called from native code.
   */
  public static void clearTestRootCertificates()
      throws NoSuchAlgorithmException, CertificateException, KeyStoreException {
    if (mUseFakeCertificateVerification) {
      FakeX509Util.clearTestRootCertificates();
    } else {
      X509Util.clearTestRootCertificates();
    }
  }
}
