package org.chromium.net.impl;

import org.chromium.net.impl.Annotations.NetError;

/**
 * Used in {@link CronetBidirectionalStream}. Implements {@link NetworkExceptionImpl}.
 */
final class BidirectionalStreamNetworkException extends NetworkExceptionImpl {
  public BidirectionalStreamNetworkException(String message, int errorCode,
                                             int cronetInternalErrorCode) {
    super(message, errorCode, cronetInternalErrorCode);
  }

  @Override
  public boolean immediatelyRetryable() {
    switch (mCronetInternalErrorCode) {
    case NetError.ERR_HTTP2_PING_FAILED:
    case NetError.ERR_QUIC_HANDSHAKE_FAILED:
      assert mErrorCode == ERROR_OTHER;
      return true;
    default:
      return super.immediatelyRetryable();
    }
  }
}
