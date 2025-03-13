package io.envoyproxy.envoymobile.engine.testing;

import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;

import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;

public final class RequestScenario {
  public int responseBufferSize = 1000;
  public boolean cancelOnResponseHeaders = false;
  public int cancelUploadOnChunk = -1;
  public boolean useDirectExecutor = false;
  public boolean waitOnReadData = false;
  public boolean useByteBufferPosition = false;
  public boolean closeBodyStream = false;
  public boolean cancelBeforeSendingRequestBody = false;

  private URL url;
  private RequestMethod method = null;
  private final List<ByteBuffer> bodyChunks = new ArrayList<>();
  private final List<Map.Entry<String, String>> headers = new ArrayList<>();
  private int trafficStatsUid = 0;
  private int trafficStatsTag = 0;

  public RequestHeaders getHeaders() {
    RequestHeadersBuilder requestHeadersBuilder =
        new RequestHeadersBuilder(method, url.getProtocol(), url.getAuthority(), url.getPath());
    headers.forEach(entry -> requestHeadersBuilder.add(entry.getKey(), entry.getValue()));
    if (trafficStatsUid != 0) {
      requestHeadersBuilder.addSocketTag(trafficStatsUid, trafficStatsTag);
    }
    return requestHeadersBuilder.build();
  }

  public List<ByteBuffer> getBodyChunks() {
    return closeBodyStream
        ? Collections.unmodifiableList(bodyChunks.subList(0, bodyChunks.size() - 1))
        : Collections.unmodifiableList(bodyChunks);
  }

  public Optional<ByteBuffer> getClosingBodyChunk() {
    return closeBodyStream ? Optional.of(bodyChunks.get(bodyChunks.size() - 1)) : Optional.empty();
  }

  public boolean hasBody() { return !bodyChunks.isEmpty(); }

  public RequestScenario setHttpMethod(RequestMethod requestMethod) {
    this.method = requestMethod;
    return this;
  }

  public RequestScenario setUrl(String url) throws MalformedURLException {
    this.url = new URL(url);
    return this;
  }

  public RequestScenario addBody(byte[] requestBodyChunk) {
    ByteBuffer byteBuffer = ByteBuffer.wrap(requestBodyChunk);
    bodyChunks.add(byteBuffer);
    return this;
  }

  public RequestScenario addBody(String requestBodyChunk) {
    return addBody(requestBodyChunk.getBytes());
  }

  public RequestScenario addBody(ByteBuffer requestBodyChunk) {
    bodyChunks.add(requestBodyChunk);
    return this;
  }

  public RequestScenario addHeader(String key, String value) {
    headers.add(new SimpleImmutableEntry<>(key, value));
    return this;
  }

  public RequestScenario setResponseBufferSize(int responseBufferSize) {
    this.responseBufferSize = responseBufferSize;
    return this;
  }

  public RequestScenario cancelOnResponseHeaders() {
    this.cancelOnResponseHeaders = true;
    return this;
  }

  public RequestScenario cancelUploadOnChunk(int chunkNo) {
    this.cancelUploadOnChunk = chunkNo;
    return this;
  }

  public RequestScenario useDirectExecutor() {
    this.useDirectExecutor = true;
    return this;
  }

  public RequestScenario waitOnReadData() {
    this.waitOnReadData = true;
    return this;
  }

  public RequestScenario closeBodyStream() {
    closeBodyStream = true;
    return this;
  }

  public RequestScenario cancelBeforeSendingRequestBody() {
    cancelBeforeSendingRequestBody = true;
    return this;
  }

  public RequestScenario useByteBufferPosition() {
    useByteBufferPosition = true;
    return this;
  }

  public RequestScenario addSocketTag(int uid, int tag) {
    trafficStatsUid = uid;
    trafficStatsTag = tag;
    return this;
  }
}
