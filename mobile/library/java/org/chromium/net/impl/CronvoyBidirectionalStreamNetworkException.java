package org.chromium.net.impl;

import org.chromium.net.impl.Errors.NetError;

/**
 * Used in {@link CronetBidirectionalStream}. Implements {@link CronvoyNetworkExceptionImpl}.
 */
public final class CronvoyBidirectionalStreamNetworkException extends CronvoyNetworkExceptionImpl {
  public CronvoyBidirectionalStreamNetworkException(String message, int errorCode,
                                                    int cronetInternalErrorCode) {
    this(message, errorCode, cronetInternalErrorCode, "");
  }

  public CronvoyBidirectionalStreamNetworkException(String message, int errorCode,
                                                    int cronetInternalErrorCode,
                                                    String errorDetails) {
    super(message, errorCode, cronetInternalErrorCode, errorDetails);
  }

  @Override
  public boolean immediatelyRetryable() {
    if (mCronetInternalErrorCode == NetError.ERR_HTTP2_PING_FAILED.getErrorCode() ||
        mCronetInternalErrorCode == NetError.ERR_QUIC_HANDSHAKE_FAILED.getErrorCode()) {
      assert mErrorCode == ERROR_OTHER;
      return true;
    }
    return super.immediatelyRetryable();
  }
}
