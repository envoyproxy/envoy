package org.chromium.net.testing;

import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification;
import static com.google.common.truth.Truth.assertThat;
import static org.chromium.net.testing.CronetTestRule.SERVER_CERT_PEM;
import static org.chromium.net.testing.CronetTestRule.SERVER_KEY_PKCS8_PEM;

import static org.junit.Assert.assertNotNull;
import android.content.Context;
import androidx.test.core.app.ApplicationProvider;

import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.EnvoyError;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.ResponseTrailers;
import io.envoyproxy.envoymobile.engine.JniLibrary;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import io.envoyproxy.envoymobile.utilities.FakeX509Util;

@RunWith(RobolectricTestRunner.class)
public class Http2TestServerTest {

  private Engine engine;
  private String serverCertPath = SERVER_CERT_PEM;
  private String serverKeyPath = SERVER_KEY_PKCS8_PEM;

  @BeforeClass
  public static void loadJniLibrary() {
    JniLibrary.loadTestLibrary();
  }

  @Before
  public void setUp() throws Exception {
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(true);
    FakeX509Util.setExpectedHost(Http2TestServer.getServerHost());
    AndroidNetworkLibrary.addTestRootCertificate(CertTestUtil.pemToDer(serverCertPath));
  }

  public void setUpEngine(boolean enablePlatformCertificatesValidation,
                          TrustChainVerification trustChainVerification) throws Exception {
    CountDownLatch latch = new CountDownLatch(1);
    Context appContext = ApplicationProvider.getApplicationContext();
    engine = new AndroidEngineBuilder(appContext)
                 .setLogLevel(LogLevel.DEBUG)
                 .setLogger((level, message) -> {
                   System.out.print(message);
                   return null;
                 })
                 .enablePlatformCertificatesValidation(enablePlatformCertificatesValidation)
                 .setTrustChainVerification(trustChainVerification)
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .build();
    Http2TestServer.startHttp2TestServer(appContext, serverCertPath, serverKeyPath);
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdown() throws Exception {
    engine.terminate();
    Http2TestServer.shutdownHttp2TestServer();
    AndroidNetworkLibrary.clearTestRootCertificates();
    AndroidNetworkLibrary.setFakeCertificateVerificationForTesting(false);
  }

  private void getSchemeIsHttps(boolean enablePlatformCertificatesValidation,
                                TrustChainVerification trustChainVerification) throws Exception {
    setUpEngine(enablePlatformCertificatesValidation, trustChainVerification);

    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(Http2TestServer.getEchoAllHeadersUrl());

    Response response = sendRequest(requestScenario);

    assertThat(response.getEnvoyError()).isNull();
    assertThat(response.getHeaders().getHttpStatus()).isEqualTo(200);
    assertThat(response.getBodyAsString()).contains(":scheme: https");
    assertThat(response.getHeaders().value("x-envoy-upstream-alpn")).containsExactly("h2");
  }

  @Test
  public void testGetRequest() throws Exception {
    getSchemeIsHttps(false, TrustChainVerification.ACCEPT_UNTRUSTED);
  }

  @Ignore
  @Test
  public void testGetRequestWithPlatformCertValidatorSuccess() throws Exception {
    getSchemeIsHttps(true, TrustChainVerification.VERIFY_TRUST_CHAIN);
  }

  @Test
  public void testGetRequestWithPlatformCertValidatorFail() throws Exception {
    // Remove any pre-installed test certs, so that following verifications will fail.
    AndroidNetworkLibrary.clearTestRootCertificates();
    setUpEngine(true, TrustChainVerification.VERIFY_TRUST_CHAIN);
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(Http2TestServer.getEchoAllHeadersUrl());
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());
    engine.streamClient()
        .newStreamPrototype()
        .setOnError((error, ignored) -> {
          response.get().setEnvoyError(error);
          latch.countDown();
          return null;
        })
        .setOnCancel((ignored) -> { throw new AssertionError("Unexpected OnCancel called."); })
        .start(Executors.newSingleThreadExecutor())
        .sendHeaders(requestScenario.getHeaders(), /* endStream= */ false, /* idempotent= */ false);

    latch.await();
    assertNotNull(response.get().getEnvoyError());
    assertThat(response.get().getEnvoyError().getErrorCode()).isEqualTo(2);
    assertThat(response.get().getEnvoyError().getMessage()).contains("CERTIFICATE_VERIFY_FAILED");
    response.get().throwAssertionErrorIfAny();
  }

  @Test
  public void testAcceptUntrustedWithPlatformCertValidator() throws Exception {
    // Remove any pre-installed test certs, so that following verifications will fail.
    AndroidNetworkLibrary.clearTestRootCertificates();
    getSchemeIsHttps(true, TrustChainVerification.ACCEPT_UNTRUSTED);
  }

  @Test
  public void testSubjectAltNameErrorWithPlatformCertValidator() throws Exception {
    // Switch to a cert which doesn't have 127.0.0.1 in the SAN list.
    serverCertPath = "../envoy/test/config/integration/certs/servercert.pem";
    serverKeyPath = "../envoy/test/config/integration/certs/serverkey.pem";
    AndroidNetworkLibrary.addTestRootCertificate(CertTestUtil.pemToDer(serverCertPath));
    setUpEngine(true, TrustChainVerification.VERIFY_TRUST_CHAIN);
    RequestScenario requestScenario = new RequestScenario()
                                          .setHttpMethod(RequestMethod.GET)
                                          .setUrl(Http2TestServer.getEchoAllHeadersUrl());
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());
    engine.streamClient()
        .newStreamPrototype()
        .setOnError((error, ignored) -> {
          response.get().setEnvoyError(error);
          latch.countDown();
          return null;
        })
        .setOnCancel((ignored) -> { throw new AssertionError("Unexpected OnCancel called."); })
        .start(Executors.newSingleThreadExecutor())
        .sendHeaders(requestScenario.getHeaders(), /* endStream= */ false, /* idempotent= */ false);

    latch.await();
    assertNotNull(response.get().getEnvoyError());
    assertThat(response.get().getEnvoyError().getErrorCode()).isEqualTo(2);
    assertThat(response.get().getEnvoyError().getMessage()).contains("CERTIFICATE_VERIFY_FAILED");
    response.get().throwAssertionErrorIfAny();
  }

  @Test
  public void testSubjectAltNameErrorAllowedWithPlatformCertValidator() throws Exception {
    // Switch to a cert which doesn't have 127.0.0.1 in the SAN list.
    serverCertPath = "../envoy/test/config/integration/certs/servercert.pem";
    serverKeyPath = "../envoy/test/config/integration/certs/serverkey.pem";
    AndroidNetworkLibrary.addTestRootCertificate(CertTestUtil.pemToDer(serverCertPath));
    getSchemeIsHttps(true, TrustChainVerification.ACCEPT_UNTRUSTED);
  }

  private Response sendRequest(RequestScenario requestScenario) throws Exception {
    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Response> response = new AtomicReference<>(new Response());

    engine.streamClient()
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
        .setOnComplete((ignore) -> {
          latch.countDown();
          return null;
        })
        .start(Executors.newSingleThreadExecutor())
        .sendHeaders(requestScenario.getHeaders(), /* hasRequestBody= */ false,
                     /* idempotent= */ false);

    latch.await();
    response.get().throwAssertionErrorIfAny();
    return response.get();
  }

  private static class RequestScenario {

    private URL url;
    private RequestMethod method = null;

    RequestHeaders getHeaders() {
      RequestHeadersBuilder requestHeadersBuilder =
          new RequestHeadersBuilder(method, url.getProtocol(), url.getAuthority(), url.getPath());
      return requestHeadersBuilder.build();
    }

    RequestScenario setHttpMethod(RequestMethod requestMethod) {
      this.method = requestMethod;
      return this;
    }

    RequestScenario setUrl(String url) throws MalformedURLException {
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
