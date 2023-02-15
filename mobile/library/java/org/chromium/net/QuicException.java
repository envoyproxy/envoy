package org.chromium.net;

/**
 * Subclass of {@link NetworkException} which contains a detailed <a
 * href="https://www.chromium.org/quic">QUIC</a> error code from <a
 * href="https://cs.chromium.org/search/?q=symbol:%5CbQuicErrorCode%5Cb">QuicErrorCode</a>. An
 * instance of {@code QuicException} is passed to {@code onFailed} callbacks when the error code is
 * {@link NetworkException#ERROR_QUIC_PROTOCOL_FAILED NetworkException.ERROR_QUIC_PROTOCOL_FAILED}.
 */
public abstract class QuicException extends NetworkException {
  /**
   * Constructs an exception that is caused by a QUIC protocol error.
   *
   * @param message explanation of failure.
   * @param cause the cause (which is saved for later retrieval by the {@link
   *     java.io.IOException#getCause getCause()} method). A null value is permitted, and indicates
   *     that the cause is nonexistent or unknown.
   */
  protected QuicException(String message, Throwable cause) { super(message, cause); }

  /**
   * Returns the <a href="https://www.chromium.org/quic">QUIC</a> error code, which is a value from
   * <a href="https://cs.chromium.org/search/?q=symbol:%5CbQuicErrorCode%5Cb">QuicErrorCode</a>.
   */
  public abstract int getQuicDetailedErrorCode();
}
