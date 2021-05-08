package org.cronvoy;

import androidx.annotation.Nullable;
import java.util.Date;
import org.chromium.net.RequestFinishedInfo;

/** Implementation of {@link RequestFinishedInfo.Metrics}. */
final class CronetMetrics extends RequestFinishedInfo.Metrics {

  private final long requestStartMs;
  private final long dnsStartMs;
  private final long dnsEndMs;
  private final long connectStartMs;
  private final long connectEndMs;
  private final long sslStartMs;
  private final long sslEndMs;
  private final long sendingStartMs;
  private final long sendingEndMs;
  private final long pushStartMs;
  private final long pushEndMs;
  private final long responseStartMs;
  private final long requestEndMs;
  private final boolean socketReused;

  @Nullable private final Long ttfbMs;
  @Nullable private final Long totalTimeMs;
  @Nullable private final Long sentByteCount;
  @Nullable private final Long receivedByteCount;

  @Nullable
  private static Date toDate(long timestamp) {
    if (timestamp != -1) {
      return new Date(timestamp);
    }
    return null;
  }

  private static boolean checkOrder(long start, long end) {
    // If end doesn't exist, start can be anything, including also not existing
    // If end exists, start must also exist and be before end
    return (end >= start && start != -1) || end == -1;
  }

  /** New-style constructor */
  public CronetMetrics(long requestStartMs, long dnsStartMs, long dnsEndMs, long connectStartMs,
                       long connectEndMs, long sslStartMs, long sslEndMs, long sendingStartMs,
                       long sendingEndMs, long pushStartMs, long pushEndMs, long responseStartMs,
                       long requestEndMs, boolean socketReused, long sentByteCount,
                       long receivedByteCount) {
    // Check that no end times are before corresponding start times,
    // or exist when start time doesn't.
    assert checkOrder(dnsStartMs, dnsEndMs);
    assert checkOrder(connectStartMs, connectEndMs);
    assert checkOrder(sslStartMs, sslEndMs);
    assert checkOrder(sendingStartMs, sendingEndMs);
    assert checkOrder(pushStartMs, pushEndMs);
    // requestEnd always exists, so just check that it's after start
    assert requestEndMs >= responseStartMs;
    // Spot-check some of the other orderings
    assert dnsStartMs >= requestStartMs || dnsStartMs == -1;
    assert sendingStartMs >= requestStartMs || sendingStartMs == -1;
    assert sslStartMs >= connectStartMs || sslStartMs == -1;
    assert responseStartMs >= sendingStartMs || responseStartMs == -1;
    this.requestStartMs = requestStartMs;
    this.dnsStartMs = dnsStartMs;
    this.dnsEndMs = dnsEndMs;
    this.connectStartMs = connectStartMs;
    this.connectEndMs = connectEndMs;
    this.sslStartMs = sslStartMs;
    this.sslEndMs = sslEndMs;
    this.sendingStartMs = sendingStartMs;
    this.sendingEndMs = sendingEndMs;
    this.pushStartMs = pushStartMs;
    this.pushEndMs = pushEndMs;
    this.responseStartMs = responseStartMs;
    this.requestEndMs = requestEndMs;
    this.socketReused = socketReused;
    this.sentByteCount = sentByteCount;
    this.receivedByteCount = receivedByteCount;

    if (requestStartMs != -1 && responseStartMs != -1) {
      ttfbMs = responseStartMs - requestStartMs;
    } else {
      ttfbMs = null;
    }
    if (requestStartMs != -1 && requestEndMs != -1) {
      totalTimeMs = requestEndMs - requestStartMs;
    } else {
      totalTimeMs = null;
    }
  }

  @Nullable
  @Override
  public Date getRequestStart() {
    return toDate(requestStartMs);
  }

  @Nullable
  @Override
  public Date getDnsStart() {
    return toDate(dnsStartMs);
  }

  @Nullable
  @Override
  public Date getDnsEnd() {
    return toDate(dnsEndMs);
  }

  @Nullable
  @Override
  public Date getConnectStart() {
    return toDate(connectStartMs);
  }

  @Nullable
  @Override
  public Date getConnectEnd() {
    return toDate(connectEndMs);
  }

  @Nullable
  @Override
  public Date getSslStart() {
    return toDate(sslStartMs);
  }

  @Nullable
  @Override
  public Date getSslEnd() {
    return toDate(sslEndMs);
  }

  @Nullable
  @Override
  public Date getSendingStart() {
    return toDate(sendingStartMs);
  }

  @Nullable
  @Override
  public Date getSendingEnd() {
    return toDate(sendingEndMs);
  }

  @Nullable
  @Override
  public Date getPushStart() {
    return toDate(pushStartMs);
  }

  @Nullable
  @Override
  public Date getPushEnd() {
    return toDate(pushEndMs);
  }

  @Nullable
  @Override
  public Date getResponseStart() {
    return toDate(responseStartMs);
  }

  @Nullable
  @Override
  public Date getRequestEnd() {
    return toDate(requestEndMs);
  }

  @Override
  public boolean getSocketReused() {
    return socketReused;
  }

  @Nullable
  @Override
  public Long getTtfbMs() {
    return ttfbMs;
  }

  @Nullable
  @Override
  public Long getTotalTimeMs() {
    return totalTimeMs;
  }

  @Nullable
  @Override
  public Long getSentByteCount() {
    return sentByteCount;
  }

  @Nullable
  @Override
  public Long getReceivedByteCount() {
    return receivedByteCount;
  }
}
