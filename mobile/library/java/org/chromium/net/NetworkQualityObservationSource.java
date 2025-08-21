package org.chromium.net;

import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@IntDef({NetworkQualityObservationSource.HTTP, NetworkQualityObservationSource.TCP,
         NetworkQualityObservationSource.QUIC, NetworkQualityObservationSource.HTTP_CACHED_ESTIMATE,
         NetworkQualityObservationSource.DEFAULT_HTTP_FROM_PLATFORM,
         NetworkQualityObservationSource.DEPRECATED_HTTP_EXTERNAL_ESTIMATE,
         NetworkQualityObservationSource.TRANSPORT_CACHED_ESTIMATE,
         NetworkQualityObservationSource.DEFAULT_TRANSPORT_FROM_PLATFORM,
         NetworkQualityObservationSource.H2_PINGS, NetworkQualityObservationSource.MAX})
@Retention(RetentionPolicy.SOURCE)
public @interface NetworkQualityObservationSource {
  /**
   * The observation was taken at the request layer, e.g., a round trip time is recorded as the time
   * between the request being sent and the first byte being received.
   */
  int HTTP = 0;
  /** The observation is taken from TCP statistics maintained by the kernel. */
  int TCP = 1;
  /** The observation is taken at the QUIC layer. */
  int QUIC = 2;
  /**
   * The observation is a previously cached estimate of the metric. The metric was computed at the
   * HTTP layer.
   */
  int HTTP_CACHED_ESTIMATE = 3;
  /**
   * The observation is derived from network connection information provided by the platform. For
   * example, typical RTT and throughput values are used for a given type of network connection. The
   * metric was provided for use at the HTTP layer.
   */
  int DEFAULT_HTTP_FROM_PLATFORM = 4;
  /**
   * The observation came from a Chromium-external source. The metric was computed by the external
   * source at the HTTP layer. Deprecated since external estimate provider is not currently queried.
   */
  int DEPRECATED_HTTP_EXTERNAL_ESTIMATE = 5;
  /**
   * The observation is a previously cached estimate of the metric. The metric was computed at the
   * transport layer.
   */
  int TRANSPORT_CACHED_ESTIMATE = 6;
  /**
   * The observation is derived from the network connection information provided by the platform.
   * For example, typical RTT and throughput values are used for a given type of network connection.
   * The metric was provided for use at the transport layer.
   */
  int DEFAULT_TRANSPORT_FROM_PLATFORM = 7;
  /** Round trip ping latency reported by H2 connections. */
  int H2_PINGS = 8;

  int MAX = 9;
}
