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
      "static_resources:\n"
      + " listeners:\n"
      + " - name: base_api_listener\n"
      + "   address:\n"
      + "     socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }\n"
      + "   api_listener:\n"
      + "     api_listener:\n"
      + "       \"@type\": " + HCM_TYPE + "\n"
      + "       stat_prefix: api_hcm\n"
      + "       route_config:\n"
      + "         name: api_router\n"
      + "         virtual_hosts:\n"
      + "         - name: api\n"
      + "           domains: [\"*\"]\n"
      + "           routes:\n"
      + "           - match: { prefix: \"/\" }\n"
      + "             route: { cluster: h3_remote }\n"
      + "       http_filters:\n"
      + "       - name: envoy.router\n"
      + "         typed_config:\n"
      + "           \"@type\": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router\n"
      + " clusters:\n"
      + " - name: h3_remote\n"
      + "   connect_timeout: 10s\n"
      + "   type: STATIC\n"
      + "   dns_lookup_family: V4_ONLY\n"
      + "   lb_policy: ROUND_ROBIN\n"
      + "   load_assignment:\n"
      + "     cluster_name: h3_remote\n"
      + "     endpoints:\n"
      + "     - lb_endpoints:\n"
      + "       - endpoint:\n"
      + "           address:\n"
      + "             socket_address: { address: 127.0.0.1, port_value: %s }\n"
      + "   typed_extension_protocol_options:\n"
      + "     envoy.extensions.upstreams.http.v3.HttpProtocolOptions:\n"
      +
      "       \"@type\": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions\n"
      + "       explicit_http_config:\n"
      + "         http3_protocol_options: {}\n"
      + "       common_http_protocol_options:\n"
      + "         idle_timeout: 1s\n"
      + "   transport_socket:\n"
      + "     name: envoy.transport_sockets.quic\n"
      + "     typed_config:\n"
      + "       \"@type\": " + QUIC_UPSTREAM_TYPE + "\n"
      + "       upstream_tls_context:\n"
      + "         sni: www.lyft.com";

  private final Context appContext = ApplicationProvider.getApplicationContext();
  private Engine engine;

  @BeforeClass
  public static void loadJniLibrary() {
    AndroidJniLibrary.loadTestLibrary();
    JniLibrary.load();
  }

  @Before
  public void setUpEngine() throws Exception {
    QuicTestServer.startQuicTestServer();
    CountDownLatch latch = new CountDownLatch(1);
    engine = new AndroidEngineBuilder(
                 appContext, new Custom(String.format(CONFIG, QuicTestServer.getServerPort())))
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
    QuicTestServer.shutdownQuicTestServer();
  }

  @Test
  public void get_simpleTxt() throws Exception {
    QuicTestServerTest.RequestScenario requestScenario =
        new QuicTestServerTest.RequestScenario()
            .setHttpMethod(RequestMethod.GET)
            .setUrl(QuicTestServer.getServerURL() + "/simple.txt");

    QuicTestServerTest.Response response = sendRequest(requestScenario);

    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString())
        .isEqualTo("This is a simple text file served by QUIC.\n");
    assertThat(response.getEnvoyError()).isNull();
  }

  private QuicTestServerTest.Response sendRequest(RequestScenario requestScenario)
      throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<QuicTestServerTest.Response> response =
        new AtomicReference<>(new QuicTestServerTest.Response());

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

  private static class RequestScenario {

    private URL url;
    private RequestMethod method = null;
    private final List<ByteBuffer> bodyChunks = new ArrayList<>();
    private final boolean closeBodyStream = false;

    RequestHeaders getHeaders() {
      RequestHeadersBuilder requestHeadersBuilder =
          new RequestHeadersBuilder(method, url.getProtocol(), url.getAuthority(), url.getPath());
      requestHeadersBuilder.add("no_trailers", "0");
      return requestHeadersBuilder.build();
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

    QuicTestServerTest.RequestScenario setHttpMethod(RequestMethod requestMethod) {
      this.method = requestMethod;
      return this;
    }

    QuicTestServerTest.RequestScenario setUrl(String url) throws MalformedURLException {
      this.url = new URL(url);
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

    void throwAssertionErrorIfAny() {
      if (assertionError.get() != null) {
        throw assertionError.get();
      }
    }
  }
}
