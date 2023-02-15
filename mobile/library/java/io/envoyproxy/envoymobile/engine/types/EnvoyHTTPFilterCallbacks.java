package io.envoyproxy.envoymobile.engine.types;

/**
 * Callbacks for asynchronous interaction with the filter.
 */
public interface EnvoyHTTPFilterCallbacks {
  /**
   * Resume filter iteration asynchronously. This will result in an on-resume invocation of the
   * filter.
   */
  void resumeIteration();

  /**
   * Reset the underlying stream idle timeout to its configured threshold. This may be useful if
   * a filter stops iteration for an extended period of time, since ordinarily timeouts will still
   * apply. This may be called periodically to continue to indicate "activity" on the stream.
   */
  void resetIdleTimer();
}
