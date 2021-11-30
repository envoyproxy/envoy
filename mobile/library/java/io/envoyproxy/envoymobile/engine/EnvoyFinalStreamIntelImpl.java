package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;

class EnvoyFinalStreamIntelImpl implements EnvoyFinalStreamIntel {
  private long requestStartMs;
  private long dnsStartMs;
  private long dnsEndMs;
  private long connectStartMs;
  private long connectEndMs;
  private long sslStartMs;
  private long sslEndMs;
  private long sendingStartMs;
  private long sendingEndMs;
  private long responseStartMs;
  private long requestEndMs;
  private boolean socketReused;
  private long sentByteCount;
  private long receivedByteCount;

  EnvoyFinalStreamIntelImpl(long[] values) {
    requestStartMs = values[0];
    dnsStartMs = values[1];
    dnsEndMs = values[2];
    connectStartMs = values[3];
    connectEndMs = values[4];
    sslStartMs = values[5];
    sslEndMs = values[6];
    sendingStartMs = values[7];
    sendingEndMs = values[8];
    responseStartMs = values[9];
    requestEndMs = values[10];
    socketReused = values[11] != 0;
    sentByteCount = values[12];
    receivedByteCount = values[13];
  }

  @Override
  public long getRequestStartMs() {
    return requestStartMs;
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
  public long getRequestEndMs() {
    return requestEndMs;
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
}
