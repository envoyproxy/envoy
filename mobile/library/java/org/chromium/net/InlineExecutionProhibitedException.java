package org.chromium.net;

import java.util.concurrent.RejectedExecutionException;

/**
 * Thrown when an executor runs a submitted runnable inline in {@link
 * java.util.concurrent.Executor#execute(Runnable)} and {@link
 * UrlRequest.Builder#allowDirectExecutor} was not called.
 */
public final class InlineExecutionProhibitedException extends RejectedExecutionException {
  public InlineExecutionProhibitedException() {
    super("Inline execution is prohibited for this request");
  }
}
