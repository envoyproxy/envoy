package org.chromium.net;

import static org.junit.Assert.assertEquals;

import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import io.envoyproxy.envoymobile.engine.JniLibrary;

import java.nio.charset.StandardCharsets;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Simple test for Certificate verification JNI layer.
 * The objective is not to test the certificate verification logic (which is faked) but instead to
 * confirm that all JNI calls go through (confirmed by checking for the fake implementation side
 * effects).
 */
@RunWith(RobolectricTestRunner.class)
public final class CertificateVerificationTest {
  static {
    AndroidJniLibrary.loadTestLibrary();
    JniLibrary.load();
  }

  private static final byte[] host = FakeX509Util.expectedHost.getBytes(StandardCharsets.UTF_8);
  private static final byte[] authType =
      FakeX509Util.expectedAuthType.getBytes(StandardCharsets.UTF_8);

  @Before
  public void setUp() throws Exception {
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(true);
  }

  @After
  public void tearDown() throws Exception {
    JniLibrary.callClearTestRootCertificateFromNative();
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(true);
  }

  @Test
  public void testChainWithNonRootCertificate() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert"};
    final byte[][] certChain = new byte[][] {fakeCertChain[0].getBytes()};

    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain, host,
                                                                                  authType);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
  }

  @Test
  public void testChainWithRootCertificate() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert"};
    final byte[][] certChain = new byte[][] {fakeCertChain[0].getBytes()};

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain,
                                                                                  authType, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.OK);
  }

  @Test
  public void testChainWithRootCertificateWrongHostname() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert"};
    final byte[][] certChain = new byte[][] {fakeCertChain[0].getBytes()};
    final String host = "wrong host";
    final byte[] hostBytes = host.getBytes(StandardCharsets.UTF_8);

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(
            certChain, authType, hostBytes);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.FAILED);
  }

  @Test
  public void testChainWithRootCertificateWrongAuthType() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert"};
    final byte[][] certChain = new byte[][] {fakeCertChain[0].getBytes()};
    final String authType = "wrong auth type";
    final byte[] authTypeBytes = authType.getBytes(StandardCharsets.UTF_8);

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(
            certChain, authTypeBytes, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.FAILED);
  }

  @Test
  public void testClearTestRootCertificate() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert"};
    final byte[][] certChain = new byte[][] {fakeCertChain[0].getBytes()};

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    JniLibrary.callClearTestRootCertificateFromNative();
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain,
                                                                                  authType, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
  }

  @Test
  public void testChainWithMultipleNonRootCertificates() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert", "another fake cert"};
    final byte[][] certChain =
        new byte[][] {fakeCertChain[0].getBytes(), fakeCertChain[1].getBytes()};

    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain,
                                                                                  authType, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
  }

  @Test
  public void testChainWithMixedCertificates() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert", "another fake cert"};
    final byte[][] certChain =
        new byte[][] {fakeCertChain[0].getBytes(), fakeCertChain[1].getBytes()};

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain,
                                                                                  authType, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.NO_TRUSTED_ROOT);
  }

  @Test
  public void testChainWithMultipleRootCertificates() throws Exception {
    final String[] fakeCertChain = new String[] {"fake cert", "another fake cert"};
    final byte[][] certChain =
        new byte[][] {fakeCertChain[0].getBytes(), fakeCertChain[1].getBytes()};

    JniLibrary.callAddTestRootCertificateFromNative(certChain[0]);
    JniLibrary.callAddTestRootCertificateFromNative(certChain[1]);
    AndroidCertVerifyResult result =
        (AndroidCertVerifyResult)JniLibrary.callCertificateVerificationFromNative(certChain,
                                                                                  authType, host);
    assertEquals(result.getStatus(), CertVerifyStatusAndroid.OK);
  }
}
