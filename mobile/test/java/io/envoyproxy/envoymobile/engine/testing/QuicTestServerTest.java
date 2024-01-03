package io.envoyproxy.envoymobile.engine.testing;

import static org.assertj.core.api.Assertions.assertThat;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Custom;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.EnvoyError;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.ResponseTrailers;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import io.envoyproxy.envoymobile.engine.JniLibrary;
import io.envoyproxy.envoymobile.engine.testing.RequestScenario;
import io.envoyproxy.envoymobile.engine.testing.Response;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class QuicTestServerTest {

  private static final String HCM_TYPE =
      "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager";

  private static final String QUIC_UPSTREAM_TYPE =
      "type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport";

  private static final String CONFIG =
      "static_resources { listeners { name: \"base_api_listener\" address { socket_address { address: \"0.0.0.0\" port_value: 10000 } } api_listener { api_listener { [type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager] { stat_prefix: \"api_hcm\" route_config { name: \"api_router\" virtual_hosts { name: \"api\" domains: \"*\" routes { match { prefix: \"/\" } route { cluster: \"h3_remote\" } } } } http_filters { name: \"envoy.router\" typed_config { [type.googleapis.com/envoy.extensions.filters.http.router.v3.Router] { } } } } } } } clusters { name: \"h3_remote\" type: STATIC connect_timeout { seconds: 10 } dns_lookup_family: V4_ONLY transport_socket { name: \"envoy.transport_sockets.quic\" typed_config { [type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport] { upstream_tls_context { sni: \"www.lyft.com\" } } } } load_assignment { cluster_name: \"h3_remote\" endpoints { lb_endpoints { endpoint { address { socket_address { address: \"127.0.0.1\" port_value: %s } } } } } } typed_extension_protocol_options { key: \"envoy.extensions.upstreams.http.v3.HttpProtocolOptions\" value { [type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions] { common_http_protocol_options { idle_timeout { seconds: 1 } } explicit_http_config { http3_protocol_options { } } } } } } } listener_manager { name: \"envoy.listener_manager_impl.api\" typed_config { [type.googleapis.com/envoy.config.listener.v3.ApiListenerManager] { } } }";

  private final Context appContext = ApplicationProvider.getApplicationContext();
  private Engine engine;

  @BeforeClass
  public static void loadJniLibrary() {
    AndroidJniLibrary.loadTestLibrary();
    JniLibrary.load();
  }

  @Before
  public void setUpEngine() throws Exception {
    TestJni.startHttp3TestServer();
    CountDownLatch latch = new CountDownLatch(1);
    engine = new AndroidEngineBuilder(appContext,
                                      new Custom(String.format(CONFIG, TestJni.getServerPort())))
                 .addLogLevel(LogLevel.WARN)
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .build();
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdownEngine() {
    engine.terminate();
    TestJni.shutdownTestServer();
  }

  @Test
  public void get_simpleTxt() throws Exception {
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .addHeader("no_trailers", "true")
                                          .setUrl(TestJni.getServerURL() + "/simple.txt");

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
                        .sendHeaders(requestScenario.getHeaders(), !requestScenario.hasBody());
    requestScenario.getBodyChunks().forEach(stream::sendData);
    requestScenario.getClosingBodyChunk().ifPresent(stream::close);
    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }
}
