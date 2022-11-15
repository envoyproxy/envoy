package org.chromium.net.impl;

import org.chromium.net.QuicException;

/**
 * Implements {@link QuicException}.
 */
public class QuicExceptionImpl extends QuicException {
  private final int mQuicDetailedErrorCode;
  private final NetworkExceptionImpl mNetworkException;

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
  public QuicExceptionImpl(String message, int errorCode, int netErrorCode,
                           int quicDetailedErrorCode) {
    super(message, null);
    mNetworkException = new NetworkExceptionImpl(message, errorCode, netErrorCode);
    mQuicDetailedErrorCode = quicDetailedErrorCode;
  }

  @Override
  public String getMessage() {
    StringBuilder b = new StringBuilder(mNetworkException.getMessage());
    b.append(", QuicDetailedErrorCode=").append(mQuicDetailedErrorCode);
    return b.toString();
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
