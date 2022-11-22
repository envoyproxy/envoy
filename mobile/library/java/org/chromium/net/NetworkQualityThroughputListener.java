package org.chromium.net;

import java.util.concurrent.Executor;

/**
 * Listener that is notified of throughput observations from the network quality estimator. {@hide}
 * as it's a prototype.
 */
public abstract class NetworkQualityThroughputListener {
  /**
   * The executor on which this listener will be notified. Set as a final field, so it can be safely
   * accessed across threads.
   */
  private final Executor mExecutor;

  /** @param executor The executor on which the observations are reported. */
  public NetworkQualityThroughputListener(Executor executor) {
    if (executor == null) {
      throw new IllegalStateException("Executor must not be null");
    }
    mExecutor = executor;
  }

  public Executor getExecutor() { return mExecutor; }

  /**
   * Reports a new throughput observation.
   *
   * @param throughputKbps the downstream throughput in kilobits per second.
   * @param whenMs milliseconds since the Epoch (January 1st 1970, 00:00:00.000).
   * @param source the observation source from {@link NetworkQualityObservationSource}.
   */
  public abstract void onThroughputObservation(int throughputKbps, long whenMs, int source);
}
