package org.cronvoy;

import org.chromium.net.QuicException;

/** Implements {@link QuicException}. */
final class QuicExceptionImpl extends QuicException {

  private final int quicDetailedErrorCode;
  private final NetworkExceptionImpl networkException;

  /**
   * Constructs an exception with a specific error.
   *
   * @param message explanation of failure.
   * @param netErrorCode Error code from <a
   *     href=https://chromium.googlesource.com/chromium/src/+/master/net/base/net_error_list.h>this
   *     list</a>.
   * @param quicDetailedErrorCode Detailed <a href="https://www.chromium.org/quic">QUIC</a> error
   *     code from <a href="https://cs.chromium.org/search/?q=symbol:%5CbQuicErrorCode%5Cb">
   *     QuicErrorCode</a>.
   */
  QuicExceptionImpl(String message, int errorCode, int netErrorCode, int quicDetailedErrorCode) {
    super(message, null);
    networkException = new NetworkExceptionImpl(message, errorCode, netErrorCode);
    this.quicDetailedErrorCode = quicDetailedErrorCode;
  }

  @Override
  public String getMessage() {
    StringBuilder b = new StringBuilder(networkException.getMessage());
    b.append(", QuicDetailedErrorCode=").append(quicDetailedErrorCode);
    return b.toString();
  }

  @Override
  public int getErrorCode() {
    return networkException.getErrorCode();
  }

  @Override
  public int getCronetInternalErrorCode() {
    return networkException.getCronetInternalErrorCode();
  }

  @Override
  public boolean immediatelyRetryable() {
    return networkException.immediatelyRetryable();
  }

  @Override
  public int getQuicDetailedErrorCode() {
    return quicDetailedErrorCode;
  }
}
