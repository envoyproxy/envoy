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
import io.envoyproxy.envoymobile.engine.testing.RequestScenario;
import io.envoyproxy.envoymobile.engine.testing.Response;
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
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .build();
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdown() throws IOException {
    engine.terminate();
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
  public void post_simple_withoutByteBufferPosition() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readUtf8()).isEqualTo("55555");
        return new MockResponse().setBody("This is my response Body");
      }
    });
    ByteBuffer requestBody = ByteBuffer.allocateDirect(5);
    requestBody.put("55555".getBytes());
    requestBody.position(3); // The position should be ignored - only capacity matters.
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("get/flowers").toString())
                                          .addBody(requestBody)
                                          .closeBodyStream();

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_simple_withByteBufferPosition() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.POST.name());
        assertThat(recordedRequest.getBody().readUtf8()).isEqualTo("This is my request body");
        return new MockResponse().setBody("This is my response Body");
      }
    });
    ByteBuffer requestBody = ByteBuffer.allocateDirect(100);
    requestBody.put("This is my request body with spurious data".getBytes());
    requestBody.position(23); // Only the first 23 bytes should be sent - the String size is 42.
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .useByteBufferPosition()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("get/flowers").toString())
                                          .addBody(requestBody)
                                          .closeBodyStream();

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
            .isEqualTo("This is the first part of my body. This is the second part of my body.");
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.POST)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .addBody("This is the first part of my body. ")
            .addBody("This is the second part of my body.")
            .closeBodyStream(); // Content-Length is not provided; must be closed explicitly.

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_chunkedBody_withBufferPosition() throws Exception {
    String firstChunk = "This is the first chunk of my request body. ";
    String secondChunk = "This is the second chunk.";
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getBody().readUtf8()).isEqualTo(firstChunk + secondChunk);
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    ByteBuffer requestBodyFirstChunk = ByteBuffer.allocateDirect(1024);
    requestBodyFirstChunk.put((firstChunk + "spurious data that should be ignored").getBytes());
    requestBodyFirstChunk.position(firstChunk.length());
    ByteBuffer requestBodySecondChunk = ByteBuffer.allocateDirect(100);
    requestBodySecondChunk.put((secondChunk + "data beyond ByteBuffer.position()").getBytes());
    requestBodySecondChunk.position(secondChunk.length());
    RequestScenario requestScenario =
        new RequestScenario()
            .useByteBufferPosition()
            .setHttpMethod(RequestMethod.POST)
            .setUrl(mockWebServer.url("get/flowers").toString())
            .addBody(requestBodyFirstChunk)
            .addBody(requestBodySecondChunk)
            .closeBodyStream(); // Content-Length is not provided; must be closed explicitly.

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  @Test
  public void post_cancelBeforeSendingRequestBody() throws Exception {
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.POST)
                                          .setUrl(mockWebServer.url("get/flowers").toString())
                                          .addBody("Request body")
                                          .cancelBeforeSendingRequestBody();

    Response response = sendRequest(requestScenario);

    assertThat(response.isCancelled()).isTrue();
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());

    Stream stream = engine.streamClient()
                        .newStreamPrototype()
                        .setUseByteBufferPosition(requestScenario.useByteBufferPosition)
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
    if (requestScenario.cancelBeforeSendingRequestBody) {
      stream.cancel();
    } else {
      requestScenario.getBodyChunks().forEach(stream::sendData);
      requestScenario.getClosingBodyChunk().ifPresent(stream::close);
    }

    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }
}
