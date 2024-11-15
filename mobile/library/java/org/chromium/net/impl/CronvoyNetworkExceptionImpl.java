package org.chromium.net.impl;

import org.chromium.net.NetworkException;

/**
 * Implements {@link NetworkException}.
 */
public class CronvoyNetworkExceptionImpl extends NetworkException {
  // Error code, one of ERROR_*
  protected final int mErrorCode;
  // Cronet internal error code.
  protected final int mCronetInternalErrorCode;
  protected final String mErrorDetails;

  /**
   * Constructs an exception with a specific error.
   *
   * @param message explanation of failure.
   * @param errorCode error code, one of {@link #ERROR_HOSTNAME_NOT_RESOLVED ERROR_*}.
   * @param cronetInternalErrorCode Cronet internal error code, one of
   * <a
   * href=https://github.com/envoyproxy/envoy/blob/5451efd9b8f8a444431197050e45ba974ed4e9d8/mobile/library/java/org/chromium/net/impl/Errors.java#L43>
   * these</a>.
   */
  public CronvoyNetworkExceptionImpl(String message, int errorCode, int cronetInternalErrorCode) {
    this(message, errorCode, cronetInternalErrorCode, "");
  }

  /**
   * Constructs an exception with a specific error and a details string.
   *
   * @param message explanation of failure.
   * @param errorCode error code, one of {@link #ERROR_HOSTNAME_NOT_RESOLVED ERROR_*}.
   * @param cronetInternalErrorCode Cronet internal error code, one of
   * <a
   * href=https://github.com/envoyproxy/envoy/blob/5451efd9b8f8a444431197050e45ba974ed4e9d8/mobile/library/java/org/chromium/net/impl/Errors.java#L43>
   * these</a>.
   * @param errorDetails a string of error details.
   */
  public CronvoyNetworkExceptionImpl(String message, int errorCode, int cronetInternalErrorCode,
                                     String errorDetails) {
    super(message, null);
    assert errorCode > 0 && errorCode < 12;
    assert cronetInternalErrorCode < 0;
    mErrorCode = errorCode;
    mCronetInternalErrorCode = cronetInternalErrorCode;
    mErrorDetails = errorDetails;
  }

  @Override
  public int getErrorCode() {
    return mErrorCode;
  }

  @Override
  public int getCronetInternalErrorCode() {
    return mCronetInternalErrorCode;
  }

  @Override
  public boolean immediatelyRetryable() {
    switch (mErrorCode) {
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
    b.append(", ErrorCode=").append(mErrorCode);
    if (mCronetInternalErrorCode != 0) {
      b.append(", InternalErrorCode=").append(mCronetInternalErrorCode);
    }
    b.append(", Retryable=").append(immediatelyRetryable());
    return b.toString();
  }

  public String getErrorDetails() { return mErrorDetails; }
}
