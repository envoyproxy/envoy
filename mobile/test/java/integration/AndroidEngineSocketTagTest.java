package test.java.integration;

import static com.google.common.truth.Truth.assertThat;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.engine.JniLibrary;
import io.envoyproxy.envoymobile.engine.testing.RequestScenario;
import io.envoyproxy.envoymobile.engine.testing.Response;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
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
public class AndroidEngineSocketTagTest {

  private final MockWebServer mockWebServer = new MockWebServer();
  private Engine engine;

  @BeforeClass
  public static void loadJniLibrary() {
    JniLibrary.loadTestLibrary();
  }

  @Before
  public void setUpEngine() throws Exception {
    CountDownLatch latch = new CountDownLatch(1);
    Context appContext = ApplicationProvider.getApplicationContext();
    engine = new AndroidEngineBuilder(appContext)
                 .setLogLevel(LogLevel.DEBUG)
                 .setLogger((level, message) -> {
                   System.out.print(message);
                   return null;
                 })
                 .enableSocketTagging(true)
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
  public void socket_tag() throws Exception {
    mockWebServer.setDispatcher(new Dispatcher() {
      @Override
      public MockResponse dispatch(RecordedRequest recordedRequest) {
        assertThat(recordedRequest.getMethod()).isEqualTo(RequestMethod.GET.name());
        assertThat(recordedRequest.getHeader("x-envoy-mobile-socket-tag")).isEqualTo(null);
        return new MockResponse().setBody("This is my response Body");
      }
    });
    mockWebServer.start();
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(mockWebServer.url("post/flowers").toString())
                                          .addSocketTag(1, 2);

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).isEqualTo("This is my response Body");
    assertThat(response.getEnvoyError()).isNull();
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());
    final AtomicReference<Stream> streamRef = new AtomicReference<>();

    Stream stream =
        engine.streamClient()
            .newStreamPrototype()
            .setOnResponseHeaders((responseHeaders, endStream, streamIntel) -> {
              response.get().setHeaders(responseHeaders);
              if (requestScenario.cancelOnResponseHeaders) {
                streamRef.get().cancel(); // Should be a noop when endStream == true
              } else {
                if (requestScenario.waitOnReadData) {
                  try {
                    Thread.sleep(100 + (int)(Math.random() * 50));
                  } catch (InterruptedException e) {
                    // Don't care
                  }
                }
                streamRef.get().readData(requestScenario.responseBufferSize);
              }
              return null;
            })
            .setOnResponseData((data, endStream, streamIntel) -> {
              response.get().addBody(data);
              if (!endStream) {
                if (requestScenario.waitOnReadData) {
                  try {
                    Thread.sleep(100 + (int)(Math.random() * 50));
                  } catch (InterruptedException e) {
                    // Don't care
                  }
                }
                streamRef.get().readData(requestScenario.responseBufferSize);
              }
              return null;
            })
            .setOnError((error, finalStreamIntel) -> {
              response.get().setEnvoyError(error);
              latch.countDown();
              return null;
            })
            .setOnCancel((finalStreamIntel) -> {
              response.get().setCancelled();
              latch.countDown();
              return null;
            })
            .setOnComplete((finalStreamIntel) -> {
              latch.countDown();
              return null;
            })
            .setExplicitFlowControl(true)
            .start(requestScenario.useDirectExecutor ? Runnable::run
                                                     : Executors.newSingleThreadExecutor());
    streamRef.set(stream); // Set before sending headers to avoid race conditions.
    stream.sendHeaders(requestScenario.getHeaders(), !requestScenario.hasBody(),
                       /* idempotent= */ false);
    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }
}
