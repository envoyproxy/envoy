package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;
import androidx.annotation.VisibleForTesting;

// This class is made public for access in tests.
public class EnvoyFinalStreamIntelImpl implements EnvoyFinalStreamIntel {
  private final long streamStartMs;
  private final long dnsStartMs;
  private final long dnsEndMs;
  private final long connectStartMs;
  private final long connectEndMs;
  private final long sslStartMs;
  private final long sslEndMs;
  private final long sendingStartMs;
  private final long sendingEndMs;
  private final long responseStartMs;
  private final long streamEndMs;
  private final boolean socketReused;
  private final long sentByteCount;
  private final long receivedByteCount;
  private final long responseFlags;
  private final long upstreamProtocol;

  EnvoyFinalStreamIntelImpl(long[] values) {
    streamStartMs = values[0];
    dnsStartMs = values[1];
    dnsEndMs = values[2];
    connectStartMs = values[3];
    connectEndMs = values[4];
    sslStartMs = values[5];
    sslEndMs = values[6];
    sendingStartMs = values[7];
    sendingEndMs = values[8];
    responseStartMs = values[9];
    streamEndMs = values[10];
    socketReused = values[11] != 0;
    sentByteCount = values[12];
    receivedByteCount = values[13];
    responseFlags = values[14];
    upstreamProtocol = values[15];
  }

  @VisibleForTesting
  public static EnvoyFinalStreamIntelImpl createForTesting(long[] values) {
    return new EnvoyFinalStreamIntelImpl(values);
  }

  @Override
  public long getStreamStartMs() {
    return streamStartMs;
  }
  @Override
  public long getDnsStartMs() {
    return dnsStartMs;
  }
  @Override
  public long getDnsEndMs() {
    return dnsEndMs;
  }
  @Override
  public long getConnectStartMs() {
    return connectStartMs;
  }
  @Override
  public long getConnectEndMs() {
    return connectEndMs;
  }
  @Override
  public long getSslStartMs() {
    return sslStartMs;
  }
  @Override
  public long getSslEndMs() {
    return sslEndMs;
  }
  @Override
  public long getSendingStartMs() {
    return sendingStartMs;
  }
  @Override
  public long getSendingEndMs() {
    return sendingEndMs;
  }
  @Override
  public long getResponseStartMs() {
    return responseStartMs;
  }
  @Override
  public long getStreamEndMs() {
    return streamEndMs;
  }
  @Override
  public boolean getSocketReused() {
    return socketReused;
  }
  @Override
  public long getSentByteCount() {
    return sentByteCount;
  }
  @Override
  public long getReceivedByteCount() {
    return receivedByteCount;
  }
  @Override
  public long getResponseFlags() {
    return responseFlags;
  }
  @Override
  public long getUpstreamProtocol() {
    return upstreamProtocol;
  }
}
