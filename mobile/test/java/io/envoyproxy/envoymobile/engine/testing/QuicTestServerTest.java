package io.envoyproxy.envoymobile.engine.testing;

import static com.google.common.truth.Truth.assertThat;
import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.engine.JniLibrary;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class QuicTestServerTest {
  private final Context appContext = ApplicationProvider.getApplicationContext();
  private Engine engine;
  private HttpTestServerFactory.HttpTestServer httpTestServer;

  @BeforeClass
  public static void loadJniLibrary() {
    JniLibrary.loadTestLibrary();
  }

  @Before
  public void setUpEngine() throws Exception {
    Map<String, String> headers = new HashMap<>();
    headers.put("Cache-Control", "max-age=0");
    headers.put("Content-Type", "text/plain");
    httpTestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP3, 0, headers,
                                                 "This is a simple text file served by QUIC.\n",
                                                 Collections.emptyMap());

    CountDownLatch latch = new CountDownLatch(1);
    engine = new AndroidEngineBuilder(appContext)
                 .setLogLevel(LogLevel.TRACE)
                 .setLogger((level, message) -> {
                   System.out.print(message);
                   return null;
                 })
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .addQuicCanonicalSuffix(".lyft.com")
                 .addQuicHint("sni.lyft.com", httpTestServer.getPort())
                 // We need to force set the upstream TLS SNI, since the alternate protocols cache
                 // will use the SNI for the hostname lookup, and for certificate leaf node
                 // matching.
                 .setUpstreamTlsSni("sni.lyft.com")
                 .setTrustChainVerification(TrustChainVerification.ACCEPT_UNTRUSTED)
                 .build();
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdownEngine() {
    engine.terminate();
    httpTestServer.shutdown();
  }

  @Test
  public void get_simpleTxt() throws Exception {
    RequestScenario requestScenario =
        new RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .addHeader("no_trailers", "true")
            .setUrl("https://" + httpTestServer.getAddress() + "/simple.txt");

    Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString())
        .isEqualTo("This is a simple text file served by QUIC.\n");
    assertThat(response.getEnvoyError()).isNull();
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());

    Stream stream = engine.streamClient()
                        .newStreamPrototype()
                        .setOnResponseHeaders((responseHeaders, endStream, ignored) -> {
                          response.get().setHeaders(responseHeaders);
                          return null;
                        })
                        .setOnResponseData((data, endStream, ignored) -> {
                          response.get().addBody(data);
                          return null;
                        })
                        .setOnResponseTrailers((trailers, ignored) -> {
                          response.get().setTrailers(trailers);
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
                        .setOnComplete((ignored) -> {
                          latch.countDown();
                          return null;
                        })
                        .start(Executors.newSingleThreadExecutor())
                        .sendHeaders(requestScenario.getHeaders(), !requestScenario.hasBody(),
                                     /* idempotent= */ false);
    requestScenario.getBodyChunks().forEach(stream::sendData);
    requestScenario.getClosingBodyChunk().ifPresent(stream::close);
    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }
}
