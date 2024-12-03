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
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
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
// ┌─────────────────────────┐                  ┌─┼─►listener_proxy│ │
// │https://api.lyft.com/ping│  ┌──────────────┬┘ │ └──────┬───────┘ │ ┌────────────┐
// │         Request         ├──►Android Engine│  │        │         │ │api.lyft.com│
// └─────────────────────────┘  └──────────────┘  │ ┌──────▼──────┐  │ └──────▲─────┘
//                                                │ │cluster_proxy│  │        │
//                                                │ └─────────────┴──┼────────┘
//                                                │                  │
//                                                └──────────────────┘
@RunWith(RobolectricTestRunner::class)
class ProxyInfoIntentPerformHTTPSRequestBadHostnameTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  private lateinit var httpProxyTestServer: HttpProxyTestServerFactory.HttpProxyTestServer
  private lateinit var httpTestServer: HttpTestServerFactory.HttpTestServer

  @Before
  fun setUp() {
    httpProxyTestServer =
      HttpProxyTestServerFactory.start(HttpProxyTestServerFactory.Type.HTTPS_PROXY)
    httpTestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP1_WITH_TLS)
  }

  @After
  fun tearDown() {
    httpProxyTestServer.shutdown()
    httpTestServer.shutdown()
  }

  @Test
  fun `attempts an HTTPs request through a proxy using an async DNS resolution that fails`() {
    val context = ApplicationProvider.getApplicationContext<Context>()
    val connectivityManager =
      context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    connectivityManager.bindProcessToNetwork(connectivityManager.activeNetwork)
    Shadows.shadowOf(connectivityManager)
      .setProxyForNetwork(
        connectivityManager.activeNetwork,
        ProxyInfo.buildDirectProxy("bad.hostname", httpProxyTestServer.port)
      )

    val onEngineRunningLatch = CountDownLatch(1)
    val onErrorLatch = CountDownLatch(1)

    context.sendBroadcast(Intent(Proxy.PROXY_CHANGE_ACTION))

    val builder = AndroidEngineBuilder(context)
    val engine =
      builder
        .setLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .enableProxying(true)
        .setOnEngineRunning { onEngineRunningLatch.countDown() }
        .setTrustChainVerification(EnvoyConfiguration.TrustChainVerification.ACCEPT_UNTRUSTED)
        .build()

    onEngineRunningLatch.await(10, TimeUnit.SECONDS)
    assertThat(onEngineRunningLatch.count).isEqualTo(0)

    val requestHeaders =
      RequestHeadersBuilder(
          method = RequestMethod.GET,
          scheme = "https",
          authority = httpTestServer.address,
          path = "/"
        )
        .build()

    engine
      .streamClient()
      .newStreamPrototype()
      .setOnError { _, _ -> onErrorLatch.countDown() }
      .start(Executors.newSingleThreadExecutor())
      .sendHeaders(requestHeaders, true)

    onErrorLatch.await(15, TimeUnit.SECONDS)
    assertThat(onErrorLatch.count).isEqualTo(0)

    engine.terminate()
  }
}
