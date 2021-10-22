package test.kotlin.integration;

import static org.assertj.core.api.Assertions.assertThat;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.EnvoyError;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.ResponseTrailers;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.UpstreamHttpProtocol;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class AndroidEnvoyFlowTest {

  private static Engine engine;

  private final MockWebServer mockWebServer = new MockWebServer();

  @BeforeClass
  public static void loadJniLibrary() {
    AndroidJniLibrary.loadTestLibrary();
  }

  @AfterClass
  public static void shutdownEngine() {
    if (engine != null) {
      engine.terminate();
    }
  }

  @Before
  public void setUpEngine() throws Exception {
    if (engine == null) {
      CountDownLatch latch = new CountDownLatch(1);
      Context appContext = ApplicationProvider.getApplicationContext();
      engine = new AndroidEngineBuilder(appContext)
                   .setOnEngineRunning(() -> {
                     latch.countDown();
                     return null;
                   })
                   .build();
      latch.await(); // Don't launch a request before initialization has completed.
    }
  }

  @After
  public void shutdownMockWebServer() throws IOException {
    mockWebServer.shutdown();
  }

  @Test
  public void get_simple() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(mockWebServer.url("get/flowers").toString());

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
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
  public void get_withThrottledBodyResponse() throws Exception {
    // Note: throttle must be long enough to trickle the chunking.
    mockWebServer.enqueue(
        new MockResponse().throttleBody(5, 1, TimeUnit.SECONDS).setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(mockWebServer.url("get/flowers").toString());

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getEnvoyError()).isNull();
    // A "terminating" empty buffer is systematically sent through the setOnResponseData callback.
    // See: https://github.com/envoyproxy/envoy-mobile/issues/1393
    assertThat(response.getNbResponseChunks()).isEqualTo(4); // 5 bytes, 5 bytes, 2, and 0 bytes
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
                                          .setUrl(mockWebServer.url("get/flowers").toString())
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
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.POST)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .addBody("This is the first part of by body. ")
            .addBody("This is the second part of by body.")
            .closeBodyStream(); // Content-Length is not provided; must be closed explicitly.

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());

    Stream stream = engine.streamClient()
                        .newStreamPrototype()
                        .setOnResponseHeaders((responseHeaders, endStream, ignored) -> {
                          response.get().setHeaders(responseHeaders);
                          if (endStream) {
                            latch.countDown();
                          }
                          return null;
                        })
                        .setOnResponseData((data, endStream, ignored) -> {
                          response.get().addBody(data);
                          if (endStream) {
                            latch.countDown();
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
                        .start(Executors.newSingleThreadExecutor())
                        .sendHeaders(requestScenario.getHeaders(), !requestScenario.hasBody());
    requestScenario.getBodyChunks().forEach(stream::sendData);
    requestScenario.getClosingBodyChunk().ifPresent(stream::close);

    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }

  private static class RequestScenario {
    private URL url;
    private RequestMethod method = null;
    private final List<ByteBuffer> bodyChunks = new ArrayList<>();
    private final List<Map.Entry<String, String>> headers = new ArrayList<>();
    private boolean closeBodyStream = false;

    RequestHeaders getHeaders() {
      RequestHeadersBuilder requestHeadersBuilder =
          new RequestHeadersBuilder(method, url.getProtocol(), url.getAuthority(), url.getPath());
      headers.forEach(entry -> requestHeadersBuilder.add(entry.getKey(), entry.getValue()));
      // HTTP1 is the only way to send HTTP requests (not HTTPS)
      return requestHeadersBuilder.addUpstreamHttpProtocol(UpstreamHttpProtocol.HTTP1).build();
    }

    List<ByteBuffer> getBodyChunks() {
      return closeBodyStream
          ? Collections.unmodifiableList(bodyChunks.subList(0, bodyChunks.size() - 1))
          : Collections.unmodifiableList(bodyChunks);
    }

    Optional<ByteBuffer> getClosingBodyChunk() {
      return closeBodyStream ? Optional.of(bodyChunks.get(bodyChunks.size() - 1))
                             : Optional.empty();
    }

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
      ByteBuffer byteBuffer = ByteBuffer.allocateDirect(requestBodyChunk.length);
      byteBuffer.put(requestBodyChunk);
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

    RequestScenario closeBodyStream() {
      closeBodyStream = true;
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
