package org.chromium.net.impl;

import static java.net.HttpURLConnection.HTTP_BAD_REQUEST;
import static java.net.HttpURLConnection.HTTP_MOVED_TEMP;
import static java.net.HttpURLConnection.HTTP_OK;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import okhttp3.mockwebserver.Dispatcher;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.chromium.net.CronetException;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.UploadDataProviders;
import org.chromium.net.UrlRequest;
import org.chromium.net.UrlResponseInfo;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class CronvoyEngineTest {

  private static final String TEST_URL_PATH = "get/flowers";

  private static org.chromium.net.impl.CronetUrlRequestContext cronvoyEngine;

  private final MockWebServer mockWebServer = new MockWebServer();

  @BeforeClass
  public static void loadJniLibrary() {
    AndroidJniLibrary.loadTestLibrary();
  }

  @AfterClass
  public static void shutdown() {
    if (cronvoyEngine != null) {
      cronvoyEngine.shutdown();
    }
  }

  @Before
  public void setUp() {
    if (cronvoyEngine == null) {
      NativeCronetEngineBuilderImpl nativeCronetEngineBuilder =
          new NativeCronetEngineBuilderImpl(ApplicationProvider.getApplicationContext());
      nativeCronetEngineBuilder.setUserAgent("Cronvoy");
      cronvoyEngine = new CronetUrlRequestContext(nativeCronetEngineBuilder);
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
    // HttpMethod is not set on purpose - should default to "GET" because there is no request body.
    RequestScenario requestScenario = new RequestScenario().addResponseBuffers(13);

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
  }

  @Test
  public void get_noBody() throws Exception {
    mockWebServer.enqueue(new MockResponse().setResponseCode(HTTP_OK));
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario().addResponseBuffers(1); // At least one byte must be available.

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEmpty();
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
  }

  @Test
  public void get_withSmallBuffers() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario().addResponseBuffers(4, 3, 5, 1);

    Response response = sendRequest(requestScenario);

    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
  }

  @Test
  public void get_withNotEnoughBuffer() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario().addResponseBuffers(11);

    // Message comes from Preconditions.checkHasRemaining()
    assertThatThrownBy(() -> sendRequest(requestScenario)).hasMessageContaining("already full");
  }

  @Test
  public void get_withThrottledBodyResponse() throws Exception {
    // Note: throttle must be long enough to trickle the chunking.
    mockWebServer.enqueue(
        new MockResponse().throttleBody(5, 1, TimeUnit.SECONDS).setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario().addResponseBuffers(13);

    Response response = sendRequest(requestScenario);

    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
    assertThat(response.getBodyAsString()).isEqualTo("hello, world");
    assertThat(response.getNbResponseChunks()).isEqualTo(3); // 5 bytes, 5 bytes, and 2 bytes
  }

  @Test
  public void get_cancelOnResponseStarted() throws Exception {
    mockWebServer.enqueue(new MockResponse().setBody("hello, world"));
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario().cancelOnResponseStarted();

    Response response = sendRequest(requestScenario);

    assertThat(response.isCancelled()).isTrue();
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
    assertThat(response.getBodyAsString()).isEmpty();
  }

  @Test
  public void post_simple() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readUtf8()).isEqualTo("This is the request Body");
        return new MockResponse().setBody("This is the response Body");
      }
    });
    mockWebServer.start();
    // HttpMethod is not set on purpose - should default to "POST" because there is a request body.
    RequestScenario requestScenario = new RequestScenario()
                                          .addResponseBuffers(30)
                                          .addHeader("content-type", "text/html")
                                          .setRequestBody("This is the request Body");

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("This is the response Body");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
  }

  @Test
  public void post_chunked() throws Exception {
    // This is getting chunked every 8192 bytes.
    byte[] requestBody = new byte[20_000];
    Arrays.fill(requestBody, (byte)'A');
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readByteArray()).isEqualTo(requestBody);
        return new MockResponse().setBody("This is the response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .addResponseBuffers(30)
                                          .setHttpMethod(RequestMethod.POST)
                                          .addHeader("content-type", "text/html")
                                          .setRequestBody(requestBody);

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("This is the response Body");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
  }

  @Test
  public void get_redirect() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        switch (recordedRequest.getRequestUrl().encodedPath()) {
        case "/get/flowers":
          return new MockResponse()
              .setResponseCode(HTTP_MOVED_TEMP)
              .setHeader("Location", "/get/chocolates");

        case "/get/chocolates":
          return new MockResponse().setBody("Everything is awesome").setResponseCode(HTTP_OK);
        }
        return new MockResponse().setResponseCode(HTTP_BAD_REQUEST);
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .addResponseBuffers(30)
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrlPath("/get/flowers")
                                          .addHeader("content-type", "text/html");

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("Everything is awesome");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
    assertThat(response.getUrlResponseInfo().getUrlChain())
        .contains("http://localhost:" + mockWebServer.getPort() + "/get/flowers",
                  "http://localhost:" + mockWebServer.getPort() + "/get/chocolates");
  }

  @Test
  public void get_redirect_withUnwantedBody() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        switch (recordedRequest.getRequestUrl().encodedPath()) {
        case "/get/flowers":
          return new MockResponse()
              .setResponseCode(HTTP_MOVED_TEMP)
              .setHeader("Location", "/get/chocolates")
              .addHeader("content-type", "text/html")
              .setBody("Unwanted response body that must be ignored - by API Contract");

        case "/get/chocolates":
          return new MockResponse().setBody("Everything is awesome").setResponseCode(HTTP_OK);
        }
        return new MockResponse().setResponseCode(HTTP_BAD_REQUEST);
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .addResponseBuffers(30)
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrlPath("/get/flowers")
                                          .addHeader("content-type", "text/html");

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("Everything is awesome");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
    assertThat(response.getUrlResponseInfo().getUrlChain())
        .contains("http://localhost:" + mockWebServer.getPort() + "/get/flowers",
                  "http://localhost:" + mockWebServer.getPort() + "/get/chocolates");
  }

  @Test
  public void post_redirect() throws Exception {
    // This is getting chunked every 8192 bytes.
    byte[] requestBody = new byte[20_000];
    Arrays.fill(requestBody, (byte)'A');
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        // The request POST body is being sent twice, as it should in a redirect case.
        assertThat(recordedRequest.getBody().readByteArray()).isEqualTo(requestBody);

        switch (recordedRequest.getRequestUrl().encodedPath()) {
        case "/get/flowers":
          return new MockResponse()
              .setResponseCode(HTTP_MOVED_TEMP)
              .setHeader("Location", "/get/chocolates");

        case "/get/chocolates":
          return new MockResponse().setBody("Everything is awesome").setResponseCode(HTTP_OK);
        }
        return new MockResponse().setResponseCode(HTTP_BAD_REQUEST);
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .addResponseBuffers(30)
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrlPath("/get/flowers")
                                          .addHeader("content-type", "text/html")
                                          .setRequestBody(requestBody);

    Response response = sendRequest(requestScenario);

    assertThat(response.getResponseCode()).isEqualTo(HTTP_OK);
    assertThat(response.getBodyAsString()).isEqualTo("Everything is awesome");
    assertThat(response.getCronetException()).withFailMessage(response.getErrorMessage()).isNull();
    assertThat(response.getUrlResponseInfo().getUrlChain())
        .contains("http://localhost:" + mockWebServer.getPort() + "/get/flowers",
                  "http://localhost:" + mockWebServer.getPort() + "/get/chocolates");
  }

  private Response sendRequest(RequestScenario requestScenario) {
    UrlRequestCallbackTester<Response> urlRequestCallbackTester = new UrlRequestCallbackTester<>();
    UrlRequestCallback testCallback = new UrlRequestCallback(
        requestScenario.responseBody, urlRequestCallbackTester, requestScenario);
    ExperimentalUrlRequest.Builder builder = cronvoyEngine.newUrlRequestBuilder(
        mockWebServer.url(requestScenario.urlPath).toString(),
        urlRequestCallbackTester.getWrappedUrlRequestCallback(testCallback),
        Executors.newSingleThreadExecutor());

    if (requestScenario.requestBody != null) {
      builder.setUploadDataProvider(UploadDataProviders.create(requestScenario.requestBody),
                                    Executors.newSingleThreadExecutor());
    }

    for (Map.Entry<String, String> entry : requestScenario.header) {
      builder.addHeader(entry.getKey(), entry.getValue());
    }
    if (requestScenario.httpMethod != null) {
      builder.setHttpMethod(requestScenario.httpMethod);
    }

    return urlRequestCallbackTester.waitForResponse(builder.build());
  }

  private static class UrlRequestCallback extends UrlRequest.Callback {
    private final UrlRequestCallbackTester<Response> urlRequestCallbackTester;
    private final List<ByteBuffer> responseBodyBuffers;
    private final ConcurrentLinkedQueue<ByteBuffer> buffers;
    private final AtomicInteger nbChunks = new AtomicInteger(0);
    private final AtomicInteger bufferLastRemaining = new AtomicInteger();
    private final RequestScenario requestScenario;

    private UrlRequestCallback(List<ByteBuffer> responseBodyBuffers,
                               UrlRequestCallbackTester<Response> urlRequestCallbackTester,
                               RequestScenario requestScenario) {
      this.responseBodyBuffers = responseBodyBuffers;
      this.urlRequestCallbackTester = urlRequestCallbackTester;
      this.requestScenario = requestScenario;
      buffers = new ConcurrentLinkedQueue<>(responseBodyBuffers);
    }

    @Override
    public void onRedirectReceived(UrlRequest urlRequest, UrlResponseInfo info,
                                   String newLocationUrl) {
      urlRequest.followRedirect();
    }

    @Override
    public void onResponseStarted(UrlRequest urlRequest, UrlResponseInfo info) {
      if (requestScenario.cancelOnResponseStarted) {
        urlRequest.cancel();
        return;
      }
      ByteBuffer buffer = buffers.peek();
      if (buffer == null) {
        throw new IllegalStateException("No response buffer provided.");
      }
      bufferLastRemaining.set(buffer.remaining());
      urlRequest.read(buffer);
    }

    @Override
    public void onReadCompleted(UrlRequest urlRequest, UrlResponseInfo info,
                                ByteBuffer byteBuffer) {
      ByteBuffer buffer = buffers.peek();
      if (byteBuffer != buffer) {
        throw new AssertionError("Can't happen...");
      }
      if (buffer.remaining() < bufferLastRemaining.get()) {
        nbChunks.incrementAndGet();
      }
      if (!buffer.hasRemaining()) {
        buffers.poll();
        buffer = buffers.peek();
        if (buffer == null) {
          buffer = ByteBuffer.allocateDirect(0); // This should crash urlRequest.read(buffer)
        }
      }
      bufferLastRemaining.set(buffer.remaining());
      urlRequest.read(buffer);
    }

    @Override
    public void onSucceeded(UrlRequest urlRequest, UrlResponseInfo info) {
      ByteBuffer buffer = buffers.peek();
      if (buffer != null && buffer.remaining() < bufferLastRemaining.get()) {
        nbChunks.incrementAndGet();
      }
      urlRequestCallbackTester.setResponse(
          new Response(info).setBody(responseBodyBuffers).setNbResponseChunks(nbChunks.get()));
    }

    @Override
    public void onFailed(UrlRequest urlRequest, UrlResponseInfo info, CronetException error) {
      urlRequestCallbackTester.setResponse(
          new Response(info).setCronetException(error).setBody(responseBodyBuffers));
    }

    @Override
    public void onCanceled(UrlRequest urlRequest, UrlResponseInfo info) {
      urlRequestCallbackTester.setResponse(
          new Response(info).setBody(responseBodyBuffers).setCancelled());
    }
  }

  private static class RequestScenario {
    String httpMethod = null; // Cronet has defaults - it is optional
    String urlPath = TEST_URL_PATH;
    byte[] requestBody = null;
    final List<Map.Entry<String, String>> header = new ArrayList<>();
    final List<ByteBuffer> responseBody = new ArrayList<>();
    boolean cancelOnResponseStarted = false;

    RequestScenario setHttpMethod(RequestMethod httpMethod) {
      this.httpMethod = httpMethod.name();
      return this;
    }

    RequestScenario setUrlPath(String urlPath) {
      this.urlPath = urlPath;
      return this;
    }

    RequestScenario setRequestBody(byte[] requestBody) {
      this.requestBody = requestBody;
      return this;
    }

    RequestScenario setRequestBody(String requestBody) {
      return setRequestBody(requestBody.getBytes());
    }

    RequestScenario addHeader(String key, String value) {
      header.add(new SimpleImmutableEntry<>(key, value));
      return this;
    }

    RequestScenario addResponseBuffers(int... sizes) {
      for (int size : sizes) {
        responseBody.add(ByteBuffer.allocateDirect(size));
      }
      return this;
    }

    RequestScenario cancelOnResponseStarted() {
      cancelOnResponseStarted = true;
      return this;
    }
  }

  private static class Response {
    private final UrlResponseInfo urlResponseInfo;
    private CronetException cronetException;
    private byte[] body;
    private boolean cancelled = false;
    private int nbResponseChunks = 0;

    Response(UrlResponseInfo urlResponseInfo) { this.urlResponseInfo = urlResponseInfo; }

    Response setCronetException(CronetException cronetException) {
      this.cronetException = cronetException;
      return this;
    }

    Response setBody(List<ByteBuffer> responseBody) {
      int totalSize = responseBody.stream().mapToInt(ByteBuffer::position).sum();
      body = new byte[totalSize];
      int pos = 0;
      for (ByteBuffer buffer : responseBody) {
        int bytesToRead = buffer.position();
        buffer.rewind();
        buffer.get(body, pos, bytesToRead);
        pos += bytesToRead;
      }
      return this;
    }

    Response setCancelled() {
      cancelled = true;
      return this;
    }

    Response setNbResponseChunks(int nbResponseChunks) {
      this.nbResponseChunks = nbResponseChunks;
      return this;
    }

    CronetException getCronetException() { return cronetException; }

    UrlResponseInfo getUrlResponseInfo() { return urlResponseInfo; }

    boolean isCancelled() { return cancelled; }

    int getResponseCode() { return urlResponseInfo.getHttpStatusCode(); }

    String getBodyAsString() { return new String(body); }

    int getNbResponseChunks() { return nbResponseChunks; }

    String getErrorMessage() {
      if (cronetException == null) {
        return "";
      }
      String causeStackTrace = "null";
      if (cronetException.getCause() != null) {
        ByteArrayOutputStream stackTraceBuffer = new ByteArrayOutputStream(100_000);
        PrintStream stackTracePrintStream = new PrintStream(stackTraceBuffer);
        cronetException.getCause().printStackTrace(stackTracePrintStream);
        causeStackTrace = stackTraceBuffer.toString();
      }
      return String.format("Exception: [%s], cause: %s", cronetException, causeStackTrace);
    }
  }
}
