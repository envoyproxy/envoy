package test.kotlin.integration.proxying

import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Proxy
import android.net.ProxyInfo
import androidx.test.core.app.ApplicationProvider
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.RequestHeadersBuilder
import io.envoyproxy.envoymobile.RequestMethod
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.TestJni
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito
import org.robolectric.RobolectricTestRunner

//                                                ┌──────────────────┐
//                                                │   Envoy Proxy    │
//                                                │ ┌──────────────┐ │
// ┌─────────────────────────┐                  ┌─┼─►listener_proxy│ │
// │https://api.lyft.com/ping│  ┌──────────────┬┘ │ └──────┬───────┘ │ ┌────────────┐
// │         Request         ├──►Android Engine│  │        │         │ │api.lyft.com│
// └─────────────────────────┘  └──────────────┘  │ ┌──────▼──────┐  │ └──────▲─────┘
//                                                │ │cluster_proxy│  │        │
//                                                │ └─────────────┴──┼────────┘
//                                                │                  │
//                                                └──────────────────┘
@RunWith(RobolectricTestRunner::class)
class PerformHTTPSRequestUsingProxy {
  init {
    JniLibrary.loadTestLibrary()
    JniLibrary.load()
  }

  @Test
  fun `performs an HTTPs request through a proxy`() {
    TestJni.startHttpsProxyTestServer()
    val port = TestJni.getServerPort()

    val context = Mockito.spy(ApplicationProvider.getApplicationContext<Context>())
    val connectivityManager: ConnectivityManager = Mockito.mock(ConnectivityManager::class.java)
    Mockito.doReturn(connectivityManager)
      .`when`(context)
      .getSystemService(Context.CONNECTIVITY_SERVICE)
    Mockito.`when`(connectivityManager.getDefaultProxy())
      .thenReturn(ProxyInfo.buildDirectProxy("127.0.0.1", port))

    val onEngineRunningLatch = CountDownLatch(1)
    val onRespondeHeadersLatch = CountDownLatch(1)

    context.sendStickyBroadcast(Intent(Proxy.PROXY_CHANGE_ACTION))

    val builder = AndroidEngineBuilder(context)
    val engine =
      builder
        .addLogLevel(LogLevel.DEBUG)
        .enableProxying(true)
        .setOnEngineRunning { onEngineRunningLatch.countDown() }
        .build()

    onEngineRunningLatch.await(10, TimeUnit.SECONDS)
    assertThat(onEngineRunningLatch.count).isEqualTo(0)

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = "api.lyft.com",
          path = "/ping"
        )
        .build()

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ ->
        val status = responseHeaders.httpStatus ?: 0L
        assertThat(status).isEqualTo(200)
        assertThat(responseHeaders.value("x-response-header-that-should-be-stripped")).isNull()
        onRespondeHeadersLatch.countDown()
      }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    onRespondeHeadersLatch.await(15, TimeUnit.SECONDS)
    assertThat(onRespondeHeadersLatch.count).isEqualTo(0)

    engine.terminate()
    TestJni.shutdownTestServer()
  }
}
