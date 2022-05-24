package org.chromium.net;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * Fake utility functions to verify X.509 certificates.
 *
 * FakeX509Util is not particularly clever: from its perspective a certificate is just a string and
 * its contents have no particular meaning. For a verification to succeed:
 * - all certificates in a chain must have been previously registered as root certificates
 * - host and authentication type must match the expected hardcoded values
 * This doesn't make much sense w.r.t. how X.509 certificates are really validated, but we're not
 * interested in mimicking that, we just want something to confirm that JNI calls have taken place.
 */
public final class FakeX509Util {
  private static final Set<String> validFakeCerts = new HashSet<String>();

  public static final String expectedAuthType = "RSA";
  public static final String expectedHost = "www.example.com";

  public static void addTestRootCertificate(byte[] rootCertBytes) {
    String fakeCertificate = new String(rootCertBytes);
    validFakeCerts.add(fakeCertificate);
  }

  public static void clearTestRootCertificates() { validFakeCerts.clear(); }

  /**
   * Performs fake certificate chain verification. Returns CertVerifyStatusAndroid.NO_TRUSTED_ROOT
   * if at least one of the certificates in the chain has not been previously registered as a root.
   * Returns CertVerifyStatusAndroid.OK if authType and host match respectively expectedAuthType and
   * expectedHost; CertVerifyStatusAndroid.FAILED otherwise.
   */
  public static AndroidCertVerifyResult verifyServerCertificates(byte[][] certChain,
                                                                 String authType, String host) {
    if (certChain == null || certChain.length == 0 || certChain[0] == null) {
      throw new IllegalArgumentException(
          "Expected non-null and non-empty certificate "
          + "chain passed as |certChain|. |certChain|=" + Arrays.deepToString(certChain));
    }

    for (byte[] cert : certChain) {
      String fakeCert = new String(cert);
      if (!validFakeCerts.contains(fakeCert)) {
        return new AndroidCertVerifyResult(CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
      }
    }

    return authType.equals(expectedAuthType) && host.equals(expectedHost)
        ? new AndroidCertVerifyResult(CertVerifyStatusAndroid.OK)
        : new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
  }
}
