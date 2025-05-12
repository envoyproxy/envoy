package io.envoyproxy.envoymobile.engine.testing;

import io.envoyproxy.envoymobile.EnvoyError;
import io.envoyproxy.envoymobile.FinalStreamIntel;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.ResponseTrailers;
import io.envoyproxy.envoymobile.StreamIntel;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public class Response {

  private final AtomicReference<ResponseHeaders> headers = new AtomicReference<>();
  private final AtomicReference<ResponseTrailers> trailers = new AtomicReference<>();
  private final AtomicReference<EnvoyError> envoyError = new AtomicReference<>();
  private final List<StreamIntel> streamIntels = new ArrayList<>();
  private final AtomicReference<FinalStreamIntel> finalStreamIntel = new AtomicReference<>();
  private final List<ByteBuffer> bodies = new ArrayList<>();
  private final AtomicBoolean cancelled = new AtomicBoolean(false);
  private final AtomicReference<AssertionError> assertionError = new AtomicReference<>();
  public int requestChunkSent = 0;

  public void setHeaders(ResponseHeaders headers) {
    if (!this.headers.compareAndSet(null, headers)) {
      assertionError.compareAndSet(
          null, new AssertionError("setOnResponseHeaders called more than once."));
    }
  }

  public void addBody(ByteBuffer body) { bodies.add(body); }

  public void setTrailers(ResponseTrailers trailers) {
    if (!this.trailers.compareAndSet(null, trailers)) {
      assertionError.compareAndSet(
          null, new AssertionError("setOnResponseTrailers called more than once."));
    }
  }

  public void addStreamIntel(StreamIntel streamIntel) { streamIntels.add(streamIntel); }

  public void setEnvoyError(EnvoyError envoyError) {
    if (!this.envoyError.compareAndSet(null, envoyError)) {
      assertionError.compareAndSet(null, new AssertionError("setOnError called more than once."));
    }
  }

  public void setFinalStreamIntel(FinalStreamIntel finalStreamIntel) {
    if (!this.finalStreamIntel.compareAndSet(null, finalStreamIntel)) {
      assertionError.compareAndSet(
          null, new AssertionError("setFinalStreamIntel called more than once."));
    }
  }

  public void setCancelled() {
    if (!cancelled.compareAndSet(false, true)) {
      assertionError.compareAndSet(null, new AssertionError("setOnCancel called more than once."));
    }
  }

  public List<StreamIntel> getStreamIntels() { return streamIntels; }

  public FinalStreamIntel getFinalStreamIntel() { return finalStreamIntel.get(); }

  public EnvoyError getEnvoyError() { return envoyError.get(); }

  public ResponseHeaders getHeaders() { return headers.get(); }

  public boolean isCancelled() { return cancelled.get(); }

  public int getRequestChunkSent() { return requestChunkSent; }

  public String getBodyAsString() {
    int totalSize = bodies.stream().mapToInt(ByteBuffer::limit).sum();
    byte[] body = new byte[totalSize];
    int pos = 0;
    for (ByteBuffer buffer : bodies) {
      int bytesToRead = buffer.limit();
      buffer.get(body, pos, bytesToRead);
      pos += bytesToRead;
    }
    return new String(body);
  }

  public int getNbResponseChunks() { return bodies.size(); }

  public void throwAssertionErrorIfAny() {
    if (assertionError.get() != null) {
      throw assertionError.get();
    }
  }
}
