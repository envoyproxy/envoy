package org.chromium.net;

import java.security.cert.CertificateEncodingException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The result of a certification verification.
 */
public final class AndroidCertVerifyResult {

  /**
   * The verification status. One of the values in CertVerifyStatusAndroid.
   */
  private final int mStatus;

  /**
   * True if the root CA in the chain is in the system store.
   */
  private final boolean mIsIssuedByKnownRoot;

  /**
   * The properly ordered certificate chain used for verification.
   */
  private final List<X509Certificate> mCertificateChain;

  public AndroidCertVerifyResult(int status, boolean isIssuedByKnownRoot,
                                 List<X509Certificate> certificateChain) {
    mStatus = status;
    mIsIssuedByKnownRoot = isIssuedByKnownRoot;
    mCertificateChain = new ArrayList<X509Certificate>(certificateChain);
  }

  public AndroidCertVerifyResult(int status) {
    mStatus = status;
    mIsIssuedByKnownRoot = false;
    mCertificateChain = Collections.<X509Certificate>emptyList();
  }

  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNative
  public int getStatus() { return mStatus; }

  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNative
  public boolean isIssuedByKnownRoot() { return mIsIssuedByKnownRoot; }

  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNative
  public byte[][] getCertificateChainEncoded() {
    byte[][] verifiedChainArray = new byte[mCertificateChain.size()][];
    try {
      for (int i = 0; i < mCertificateChain.size(); i++) {
        verifiedChainArray[i] = mCertificateChain.get(i).getEncoded();
      }
    } catch (CertificateEncodingException e) {
      return new byte[0][];
    }
    return verifiedChainArray;
  }
}
