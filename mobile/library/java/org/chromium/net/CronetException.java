package org.chromium.net;

import java.io.IOException;

/** Base exception passed to {@link UrlRequest.Callback#onFailed UrlRequest.Callback.onFailed()}. */
public abstract class CronetException extends IOException {
  /**
   * Constructs an exception that is caused by {@code cause}.
   *
   * @param message explanation of failure.
   * @param cause the cause (which is saved for later retrieval by the {@link
   *     java.io.IOException#getCause getCause()} method). A null value is permitted, and indicates
   *     that the cause is nonexistent or unknown.
   */
  protected CronetException(String message, Throwable cause) { super(message, cause); }
}
