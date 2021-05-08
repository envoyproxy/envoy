package org.cronvoy;

import org.chromium.net.NetworkException;

/** Implements {@link NetworkException}. */
final class NetworkExceptionImpl extends NetworkException {

  // Error code, one of ERROR_*
  protected final int errorCode;
  // Cronet internal error code.
  protected final int cronetInternalErrorCode;

  /**
   * Constructs an exception with a specific error.
   *
   * @param message explanation of failure.
   * @param errorCode error code, one of {@link #ERROR_HOSTNAME_NOT_RESOLVED ERROR_*}.
   * @param cronetInternalErrorCode Cronet internal error code, one of <a
   *     href=https://chromium.googlesource.com/chromium/src/+/master/net/base/net_error_list.h>
   *     these</a>.
   */
  NetworkExceptionImpl(String message, int errorCode, int cronetInternalErrorCode) {
    super(message, null);
    assert errorCode > 0 && errorCode < 12;
    assert cronetInternalErrorCode < 0;
    this.errorCode = errorCode;
    this.cronetInternalErrorCode = cronetInternalErrorCode;
  }

  @Override
  public int getErrorCode() {
    return errorCode;
  }

  @Override
  public int getCronetInternalErrorCode() {
    return cronetInternalErrorCode;
  }

  @Override
  public boolean immediatelyRetryable() {
    switch (errorCode) {
    case ERROR_HOSTNAME_NOT_RESOLVED:
    case ERROR_INTERNET_DISCONNECTED:
    case ERROR_CONNECTION_REFUSED:
    case ERROR_ADDRESS_UNREACHABLE:
    case ERROR_OTHER:
    default:
      return false;
    case ERROR_NETWORK_CHANGED:
    case ERROR_TIMED_OUT:
    case ERROR_CONNECTION_CLOSED:
    case ERROR_CONNECTION_TIMED_OUT:
    case ERROR_CONNECTION_RESET:
      return true;
    }
  }

  @Override
  public String getMessage() {
    StringBuilder b = new StringBuilder(super.getMessage());
    b.append(", ErrorCode=").append(errorCode);
    if (cronetInternalErrorCode != 0) {
      b.append(", InternalErrorCode=").append(cronetInternalErrorCode);
    }
    b.append(", Retryable=").append(immediatelyRetryable());
    return b.toString();
  }
}
