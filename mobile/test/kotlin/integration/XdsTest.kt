package test.kotlin.integration

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.XdsBuilder
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.XdsTestServerFactory
import java.io.File
import java.util.concurrent.CountDownLatch
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class XdsTest {
  private val appContext: Context = ApplicationProvider.getApplicationContext()
  private lateinit var engine: Engine

  init {
    AndroidJniLibrary.loadTestLibrary()
    JniLibrary.load()
  }

  private lateinit var xdsTestServer: XdsTestServerFactory.XdsTestServer

  @Before
  fun setUp() {
    val upstreamCert: String =
      File("../envoy/test/config/integration/certs/upstreamcacert.pem").readText()
    xdsTestServer = XdsTestServerFactory.create()
    val latch = CountDownLatch(1)
    engine =
      AndroidEngineBuilder(appContext)
        .addLogLevel(LogLevel.DEBUG)
        .setLogger { _, msg -> print(msg) }
        .setOnEngineRunning { latch.countDown() }
        .setXds(
          XdsBuilder(
              xdsTestServer.host,
              xdsTestServer.port,
            )
            .setSslRootCerts(upstreamCert)
            .addClusterDiscoveryService()
        )
        .build()
    latch.await()
    xdsTestServer.start()
  }

  @After
  fun tearDown() {
    engine.terminate()
    xdsTestServer.shutdown()
  }

  @Test
  fun `test xDS with CDS`() {
    // There are 2 initial clusters: base and base_clear.
    engine.waitForStatGe("cluster_manager.cluster_added", 2)
    xdsTestServer.sendDiscoveryResponse("my_cluster")
    // There are now 3 clusters: base, base_cluster, and my_cluster.
    engine.waitForStatGe("cluster_manager.cluster_added", 3)
  }
}
