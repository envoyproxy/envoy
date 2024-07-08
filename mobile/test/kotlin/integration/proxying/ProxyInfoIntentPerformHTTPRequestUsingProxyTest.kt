package test.kotlin.integration.proxying

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Proxy
import android.net.ProxyInfo
import androidx.test.core.app.ApplicationProvider
import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.HttpProxyTestServerFactory
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito
import org.robolectric.RobolectricTestRunner

//                                               ┌──────────────────┐
//                                               │   Envoy Proxy    │
//                                               │ ┌──────────────┐ │
// ┌────────────────────────┐                  ┌─┼─►listener_proxy│ │
// │http://api.lyft.com/ping│  ┌──────────────┬┘ │ └──────┬───────┘ │ ┌────────────┐
// │        Request         ├──►Android Engine│  │        │         │ │api.lyft.com│
// └────────────────────────┘  └──────────────┘  │ ┌──────▼──────┐  │ └──────▲─────┘
//                                               │ │cluster_proxy│  │        │
//                                               │ └─────────────┴──┼────────┘
//                                               │                  │
//                                               └──────────────────┘
@RunWith(RobolectricTestRunner::class)
class ProxyInfoIntentPerformHTTPRequestUsingProxyTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpProxyTestServer: HttpProxyTestServerFactory.HttpProxyTestServer

  @Before
  fun setUp() {
    httpProxyTestServer =
      HttpProxyTestServerFactory.start(HttpProxyTestServerFactory.Type.HTTP_PROXY)
  }

  @After
  fun tearDown() {
    httpProxyTestServer.shutdown()
  }

  @Test
  fun `performs an HTTP request through a proxy`() {
    val context = Mockito.spy(ApplicationProvider.getApplicationContext<Context>())
    val connectivityManager: ConnectivityManager = Mockito.mock(ConnectivityManager::class.java)
    Mockito.doReturn(connectivityManager)
      .`when`(context)
      .getSystemService(Context.CONNECTIVITY_SERVICE)
    Mockito.`when`(connectivityManager.defaultProxy)
      .thenReturn(ProxyInfo.buildDirectProxy("127.0.0.1", httpProxyTestServer.port))

    val onEngineRunningLatch = CountDownLatch(1)
    val onRespondeHeadersLatch = CountDownLatch(1)

    context.sendStickyBroadcast(Intent(Proxy.PROXY_CHANGE_ACTION))

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
          authority = "api.lyft.com",
          path = "/ping"
        )
        .build()

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        val status = responseHeaders.httpStatus ?: 0L
        assertThat(status).isEqualTo(301)
        assertThat(responseHeaders.value("x-proxy-response")).isEqualTo(listOf("true"))
        onRespondeHeadersLatch.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    onRespondeHeadersLatch.await(15, TimeUnit.SECONDS)
    assertThat(onRespondeHeadersLatch.count).isEqualTo(0)

    engine.terminate()
  }
}
