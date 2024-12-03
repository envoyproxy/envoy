package test.kotlin.integration.proxying

import android.content.Context
import android.net.ConnectivityManager
import android.net.ProxyInfo
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpProxyTestServerFactory
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows

//                                                ┌──────────────────┐
//                                                │   Envoy Proxy    │
//                                                │ ┌──────────────┐ │
// ┌─────────────────────────┐                    │ │listener_proxy│ │
// │https://api.lyft.com/ping│  ┌──────────────┐  │ └──────┬───────┘ │ ┌────────────┐
// │       HTTP Request      ├──►Android Engine│  │        │         │ │api.lyft.com│
// └─────────────────────────┘  └───────┬──────┘  │ ┌──────▼──────┐  │ └──────▲─────┘
//                                      │         │ │cluster_proxy│  │        │
//                                      │         │ └─────────────┘  │        │
//                                      │         │                  │        │
//                                      │         └──────────────────┘        │
//                                      │                                     │
//                                      └─────────────────────────────────────┘
@RunWith(RobolectricTestRunner::class)
class ProxyPollPerformHTTPRequestWithoutUsingPACProxyTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpProxyTestServer: HttpProxyTestServerFactory.HttpProxyTestServer
  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpProxyTestServer =
      HttpProxyTestServerFactory.start(HttpProxyTestServerFactory.Type.HTTP_PROXY)
    httpTestServer =
      HttpTestServerFactory.start(
        HttpTestServerFactory.Type.HTTP1_WITHOUT_TLS,
        0,
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Proxy_servers_and_tunneling/Proxy_Auto-Configuration_PAC_file
        mapOf("Content-Type" to "application/x-ns-proxy-autoconfig"),
        """
function FindProxyForURL(url, host) {
  return "PROXY ${httpProxyTestServer.ipAddress}:${httpProxyTestServer.port}";
}
      """
          .trimIndent(),
        mapOf()
      )
  }

  @After
  fun tearDown() {
    httpProxyTestServer.shutdown()
    httpTestServer.shutdown()
  }

  @Test
  fun `performs an HTTP request through a proxy`() {
    val context = ApplicationProvider.getApplicationContext<Context>()
    val connectivityManager =
      context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    connectivityManager.bindProcessToNetwork(connectivityManager.activeNetwork)
    Shadows.shadowOf(connectivityManager)
      .setProxyForNetwork(
        connectivityManager.activeNetwork,
        ProxyInfo.buildPacProxy(Uri.parse("http://${httpTestServer.address}"))
      )

    val onEngineRunningLatch = CountDownLatch(1)
    val onResponseHeadersLatch = CountDownLatch(1)

    val builder = AndroidEngineBuilder(context)
    val engine =
      builder
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .enableProxying(true)
        .setOnEngineRunning { onEngineRunningLatch.countDown() }
        .build()

    onEngineRunningLatch.await(10, TimeUnit.SECONDS)
    assertThat(onEngineRunningLatch.count).isEqualTo(0)

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "http",
          authority = httpTestServer.address,
          path = "/"
        )
        .build()

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        val status = responseHeaders.httpStatus ?: 0L
        assertThat(status).isEqualTo(200)
        assertThat(responseHeaders.value("x-proxy-response")).isNull()
        onResponseHeadersLatch.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    onResponseHeadersLatch.await(15, TimeUnit.SECONDS)
    assertThat(onResponseHeadersLatch.count).isEqualTo(0)

    engine.terminate()
  }
}
