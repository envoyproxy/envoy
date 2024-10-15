package org.chromium.net.testing;

import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED;
import static com.google.common.truth.Truth.assertThat;
import static org.chromium.net.testing.CronetTestRule.SERVER_CERT_PEM;
import static org.chromium.net.testing.CronetTestRule.SERVER_KEY_PKCS8_PEM;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.engine.JniLibrary;

import java.net.URL;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class AndroidEnvoyExplicitH2FlowTest {

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
                 .setTrustChainVerification(ACCEPT_UNTRUSTED)
                 .setLogLevel(LogLevel.DEBUG)
                 .setLogger((level, message) -> {
                   System.out.print(message);
                   return null;
                 })
                 .setOnEngineRunning(() -> {
                   latch.countDown();
                   return null;
                 })
                 .build();
    Http2TestServer.startHttp2TestServer(appContext, SERVER_CERT_PEM, SERVER_KEY_PKCS8_PEM);
    latch.await(); // Don't launch a request before initialization has completed.
  }

  @After
  public void shutdown() throws Exception {
    engine.terminate();
    Http2TestServer.shutdownHttp2TestServer();
  }

  @Test
  public void continuousWrite_withCancelOnResponseHeaders() throws Exception {
    URL url = new URL(Http2TestServer.getEchoAllHeadersUrl());
    RequestHeadersBuilder requestHeadersBuilder = new RequestHeadersBuilder(
        RequestMethod.POST, url.getProtocol(), url.getAuthority(), url.getPath());
    RequestHeaders requestHeaders = requestHeadersBuilder.build();

    final CountDownLatch latch = new CountDownLatch(1);
    final AtomicReference<Stream> stream = new AtomicReference<>();
    final AtomicInteger bufferSent = new AtomicInteger(0);

    // Loop 100,000 times which should be long enough to wait for the server's
    // response headers to arrive.
    final int numWrites = 100000;
    stream.set(
        engine.streamClient()
            .newStreamPrototype()
            .setExplicitFlowControl(true)
            .setOnSendWindowAvailable((streamIntel -> {
              ByteBuffer bf = ByteBuffer.allocateDirect(1);
              bf.put((byte)'a');
              if (bufferSent.incrementAndGet() == numWrites) {
                stream.get().close(bf);
              } else {
                stream.get().sendData(bf);
              }
              return null;
            }))
            .setOnResponseHeaders((responseHeaders, endStream, ignored) -> {
              // This was getting executed, even in the initial test, but only
              // after all the data was sent. With the fix, this should happen
              // before all the data is sent which is checked in the assert
              // below.
              stream.get().cancel();
              return null;
            })
            .setOnCancel((ignored) -> {
              latch.countDown();
              return null;
            })
            .start(Runnable::run) // direct executor - all the logic runs on the EM Network Thread.
            .sendHeaders(requestHeaders, /* endStream= */ false, /* idempotent= */ false));
    ByteBuffer bf = ByteBuffer.allocateDirect(1);
    bf.put((byte)'a');
    stream.get().sendData(bf);

    latch.await();

    assertThat(bufferSent.get()).isNotEqualTo(numWrites);
  }
}
