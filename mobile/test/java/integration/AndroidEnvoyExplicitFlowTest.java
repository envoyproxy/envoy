package test.kotlin.integration;

import static org.assertj.core.api.Assertions.assertThat;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.EnvoyError;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.ResponseTrailers;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.UpstreamHttpProtocol;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import okhttp3.mockwebserver.Dispatcher;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class AndroidEnvoyExplicitFlowTest {

  private final MockWebServer mockWebServer = new MockWebServer();
  private Engine engine;

  @BeforeClass
  public static void loadJniLibrary() {
    AndroidJniLibrary.loadTestLibrary();
  }

  @Before
  public void setUpEngine() throws Exception {
    CountDownLatch latch = new CountDownLatch(1);
    Context appContext = ApplicationProvider.getApplicationContext();
    engine = new AndroidEngineBuilder(appContext)
                 .addLogLevel(LogLevel.INFO)
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .build();
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdownEngine() throws Exception {
    engine.terminate();
    mockWebServer.shutdown();
  }

  @Test
  public void get_simple() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .setResponseBufferSize(20); // Larger than the response body size

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.getNbResponseChunks()).isEqualTo(1);
  }

  @Test
  public void get_bufferSmallerThanResponseBody() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .setResponseBufferSize(4); // Smaller than the response body size

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.getNbResponseChunks()).isEqualTo(3); // response size: 12, buffer size: 4
  }

  @Test
  public void get_withDirectExecutor() throws Exception {
    mockWebServer.start();
    for (int i = 0; i < 100; i++) {
      mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
      RequestScenario requestScenario =
          new RequestScenario()
              .useDirectExecutor()
              .setHttpMethod(RequestMethod.GET)
              .setUrl(mockWebServer.url("get/flowers").toString())
              .setResponseBufferSize(20); // Larger than the response body size

      Response response = sendRequest(requestScenario);

      assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
      assertThat(response.getBodyAsString()).isEqualTo("hello, world");
      assertThat(response.getEnvoyError()).isNull();
    }
  }

  @Test
  public void get_noBody() throws Exception {
    mockWebServer.enqueue(new MockResponse().setResponseCode(200));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(mockWebServer.url("get/flowers").toString());

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEmpty();
    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.getNbResponseChunks()).isZero();
  }

  @Test
  public void get_cancelOnResponseHeaders() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .setResponseBufferSize(20) // Larger than the response body size
            .cancelOnResponseHeaders();

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEmpty();
    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.isCancelled()).isTrue();
  }

  @Test
  public void get_noBody_cancelOnResponseHeaders() throws Exception {
    mockWebServer.enqueue(new MockResponse().setResponseCode(200));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .cancelOnResponseHeaders(); // no body ==> endStream is true for OnResponseHeaders

    Response response = sendRequest(requestScenario);
    Thread.sleep(100); // If the Stream processes a spurious onCancel callback, we will notice.

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEmpty();
    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.isCancelled()).isFalse(); // EndStream was already reached - no callback.
  }

  @Test
  public void get_withThrottledBodyResponse_bufferLargerThanResponseBody() throws Exception {
    // Note: throttle must be long enough to trickle the chunking. Chunk size is 5 bytes.
    mockWebServer.enqueue(
        new MockResponse().throttleBody(5, 1, TimeUnit.SECONDS).setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .setResponseBufferSize(6); // Larger than one chunk of the response body size

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
    // A "terminating" empty buffer is systematically sent through the setOnResponseData callback.
    // See: https://github.com/envoyproxy/envoy-mobile/issues/1393
    assertThat(response.getNbResponseChunks()).isEqualTo(4); // 5 bytes, 5 bytes, 2, and 0 bytes
  }

  @Test
  public void get_withThrottledBodyResponse_bufferSmallerThanResponseBody() throws Exception {
    // Note: throttle must be long enough to trickle the chunking.
    mockWebServer.enqueue(
        new MockResponse().throttleBody(5, 1, TimeUnit.SECONDS).setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .setResponseBufferSize(3); // Smaller than some chunk of the response body size

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
    // A "terminating" empty buffer is systematically sent through the setOnResponseData callback.
    // See: https://github.com/envoyproxy/envoy-mobile/issues/1393
    assertThat(response.getNbResponseChunks()).isEqualTo(6); // 3&2 bytes, 3&2 bytes, 2, and 0 bytes
  }

  @Test
  public void get_veryLargeResponse() throws Exception {
    byte[] responseBytes = new byte[100_000_000];
    Arrays.fill(responseBytes, (byte)'A');
    String responseBody = new String(responseBytes);
    mockWebServer.enqueue(new MockResponse().setBody(responseBody));
    mockWebServer.start();

    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(mockWebServer.url("get/flowers").toString())
                                          .setResponseBufferSize(1_000_001);

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo(responseBody);
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_simple() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readUtf8()).isEqualTo("This is my request body");
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("post/flowers").toString())
                                          .addHeader("content-length", "23")
                                          .addBody("This is my request body");

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_chunkedBody() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readUtf8())
            .isEqualTo("This is the first part of by body. This is the second part of by body.");
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("post/flowers").toString())
                                          .addBody("This is the first part of by body. ")
                                          .addBody("This is the second part of by body.");

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void put_bigBody() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.PUT.name());
        assertThat(recordedRequest.getBody().size()).isEqualTo(100_000_000);
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.PUT)
                                          .setUrl(mockWebServer.url("put/flowers").toString());
    byte[] buf = new byte[10_000];
    for (int chunckNo = 0; chunckNo < 10000; chunckNo++) { // total = 100MB
      requestScenario.addBody(buf);
    }

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_cancelUploadOnChunkZero() throws Exception {
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("post/flowers").toString())
                                          .addBody("Chunk0")
                                          .cancelUploadOnChunk(0);

    Response response = sendRequest(requestScenario);

    assertThat(response.isCancelled()).isTrue();
    assertThat(response.getRequestChunkSent()).isZero();
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_cancelUploadOnChunkOne() throws Exception {
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("post/flowers").toString())
                                          .addBody("Chunk0")
                                          .addBody("Chunk1")
                                          .cancelUploadOnChunk(1);

    Response response = sendRequest(requestScenario);

    assertThat(response.isCancelled()).isTrue();
    assertThat(response.getRequestChunkSent()).isOne();
    assertThat(response.getEnvoyError()).isNull();
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());
    final AtomicReference<Stream> streamRef = new AtomicReference<>();
    final Iterator<ByteBuffer> chunkIterator = requestScenario.getBodyChunks().iterator();

    Stream stream =
        engine.streamClient()
            .newStreamPrototype()
            .setOnSendWindowAvailable(ignored -> {
              onSendWindowAvailable(requestScenario, streamRef.get(), chunkIterator,
                                    response.get());
              return null;
            })
            .setOnResponseHeaders((responseHeaders, endStream, ignored) -> {
              response.get().setHeaders(responseHeaders);
              if (requestScenario.cancelOnResponseHeaders) {
                streamRef.get().cancel(); // Should be a noop when endStream == true
              } else {
                streamRef.get().readData(requestScenario.responseBufferSize);
              }
              if (endStream) {
                latch.countDown();
              }
              return null;
            })
            .setOnResponseData((data, endStream, ignored) -> {
              response.get().addBody(data);
              if (endStream) {
                latch.countDown();
              } else {
                streamRef.get().readData(requestScenario.responseBufferSize);
              }
              return null;
            })
            .setOnResponseTrailers((trailers, ignored) -> {
              response.get().setTrailers(trailers);
              latch.countDown();
              return null;
            })
            .setOnError((error, ignored) -> {
              response.get().setEnvoyError(error);
              latch.countDown();
              return null;
            })
            .setOnCancel((ignored) -> {
              response.get().setCancelled();
              latch.countDown();
              return null;
            })
            .setExplicitFlowControl(true)
            .start(requestScenario.useDirectExecutor ? Runnable::run
                                                     : Executors.newSingleThreadExecutor());
    streamRef.set(stream); // Set before sending headers to avoid race conditions.
    stream.sendHeaders(requestScenario.getHeaders(), !requestScenario.hasBody());
    if (requestScenario.hasBody()) {
      // The first "send" is assumes that the window is available - API contract.
      onSendWindowAvailable(requestScenario, streamRef.get(), chunkIterator, response.get());
    }
    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }

  private static void onSendWindowAvailable(RequestScenario requestScenario, Stream stream,
                                            Iterator<ByteBuffer> chunkIterator, Response response) {
    if (requestScenario.cancelUploadOnChunk == response.requestChunkSent) {
      stream.cancel();
      return;
    }
    response.requestChunkSent++;
    if (chunkIterator.hasNext()) {
      stream.sendData(chunkIterator.next());
    } else {
      stream.close(requestScenario.getClosingBodyChunk());
    }
  }

  private static class RequestScenario {
    private URL url;
    private RequestMethod method = null;
    private final List<ByteBuffer> bodyChunks = new ArrayList<>();
    private final List<Map.Entry<String, String>> headers = new ArrayList<>();
    private int responseBufferSize = 1000;
    private boolean cancelOnResponseHeaders = false;
    private int cancelUploadOnChunk = -1;
    private boolean useDirectExecutor = false;

    RequestHeaders getHeaders() {
      RequestHeadersBuilder requestHeadersBuilder =
          new RequestHeadersBuilder(method, url.getProtocol(), url.getAuthority(), url.getPath());
      headers.forEach(entry -> requestHeadersBuilder.add(entry.getKey(), entry.getValue()));
      // HTTP1 is the only way to send HTTP requests (not HTTPS)
      return requestHeadersBuilder.addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP1).build();
    }

    List<ByteBuffer> getBodyChunks() {
      return Collections.unmodifiableList(
          bodyChunks.subList(0, Math.max(bodyChunks.size() - 1, 0)));
    }

    ByteBuffer getClosingBodyChunk() { return bodyChunks.get(bodyChunks.size() - 1); }

    boolean hasBody() { return !bodyChunks.isEmpty(); }

    RequestScenario setHttpMethod(RequestMethod requestMethod) {
      this.method = requestMethod;
      return this;
    }

    RequestScenario setUrl(String url) throws MalformedURLException {
      this.url = new URL(url);
      return this;
    }

    RequestScenario addBody(byte[] requestBodyChunk) {
      ByteBuffer byteBuffer = ByteBuffer.wrap(requestBodyChunk);
      bodyChunks.add(byteBuffer);
      return this;
    }

    RequestScenario addBody(String requestBodyChunk) {
      return addBody(requestBodyChunk.getBytes());
    }

    RequestScenario addHeader(String key, String value) {
      headers.add(new SimpleImmutableEntry<>(key, value));
      return this;
    }

    RequestScenario setResponseBufferSize(int responseBufferSize) {
      this.responseBufferSize = responseBufferSize;
      return this;
    }

    RequestScenario cancelOnResponseHeaders() {
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
  }

  private static class Response {
    private final AtomicReference<ResponseHeaders> headers = new AtomicReference<>();
    private final AtomicReference<ResponseTrailers> trailers = new AtomicReference<>();
    private final AtomicReference<EnvoyError> envoyError = new AtomicReference<>();
    private final List<ByteBuffer> bodies = new ArrayList<>();
    private final AtomicBoolean cancelled = new AtomicBoolean(false);
    private final AtomicReference<AssertionError> assertionError = new AtomicReference<>();
    private int requestChunkSent = 0;

    void setHeaders(ResponseHeaders headers) {
      if (!this.headers.compareAndSet(null, headers)) {
        assertionError.compareAndSet(
            null, new AssertionError("setOnResponseHeaders called more than once."));
      }
    }

    void addBody(ByteBuffer body) { bodies.add(body); }

    void setTrailers(ResponseTrailers trailers) {
      if (!this.trailers.compareAndSet(null, trailers)) {
        assertionError.compareAndSet(
            null, new AssertionError("setOnResponseTrailers called more than once."));
      }
    }

    void setEnvoyError(EnvoyError envoyError) {
      if (!this.envoyError.compareAndSet(null, envoyError)) {
        assertionError.compareAndSet(null, new AssertionError("setOnError called more than once."));
      }
    }

    void setCancelled() {
      if (!cancelled.compareAndSet(false, true)) {
        assertionError.compareAndSet(null,
                                     new AssertionError("setOnCancel called more than once."));
      }
    }

    EnvoyError getEnvoyError() { return envoyError.get(); }

    ResponseHeaders getHeaders() { return headers.get(); }

    ResponseTrailers getTrailers() { return trailers.get(); }

    boolean isCancelled() { return cancelled.get(); }

    int getRequestChunkSent() { return requestChunkSent; }

    String getBodyAsString() {
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

    int getNbResponseChunks() { return bodies.size(); }

    void throwAssertionErrorIfAny() {
      if (assertionError.get() != null) {
        throw assertionError.get();
      }
    }
  }
}
