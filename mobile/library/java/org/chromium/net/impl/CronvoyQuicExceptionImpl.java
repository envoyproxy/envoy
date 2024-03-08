package org.chromium.net.impl;

import org.chromium.net.QuicException;

/**
 * Implements {@link QuicException}.
 */
public class CronvoyQuicExceptionImpl extends QuicException {
  private final int mQuicDetailedErrorCode;
  private final CronvoyNetworkExceptionImpl mNetworkException;

  /**
   * Constructs an exception with a specific error.
   *
   * @param message explanation of failure.
   * @param netErrorCode Error code from
   * <a href=https://chromium.googlesource.com/chromium/src/+/master/net/base/net_error_list.h>
   * this list</a>.
   * @param quicDetailedErrorCode Detailed <a href="https://www.chromium.org/quic">QUIC</a> error
   * code from <a
   * href="https://cs.chromium.org/search/?q=symbol:%5CbQuicErrorCode%5Cb">
   * QuicErrorCode</a>.
   */
  public CronvoyQuicExceptionImpl(String message, int errorCode, int netErrorCode,
                                  int quicDetailedErrorCode) {
    super(message, null);
    mNetworkException = new CronvoyNetworkExceptionImpl(message, errorCode, netErrorCode);
    mQuicDetailedErrorCode = quicDetailedErrorCode;
  }

  @Override
  public String getMessage() {
    return mNetworkException.getMessage() + ", QuicDetailedErrorCode=" + mQuicDetailedErrorCode;
  }

  @Override
  public int getErrorCode() {
    return mNetworkException.getErrorCode();
  }

  @Override
  public int getCronetInternalErrorCode() {
    return mNetworkException.getCronetInternalErrorCode();
  }

  @Override
  public boolean immediatelyRetryable() {
    return mNetworkException.immediatelyRetryable();
  }

  @Override
  public int getQuicDetailedErrorCode() {
    return mQuicDetailedErrorCode;
  }
}
