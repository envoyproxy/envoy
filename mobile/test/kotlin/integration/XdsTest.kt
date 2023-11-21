package test.kotlin.integration

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.Engine
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.XdsBuilder
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary
import io.envoyproxy.envoymobile.engine.JniLibrary
import io.envoyproxy.envoymobile.engine.testing.TestJni
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

  @Before
  fun setUp() {
    val upstreamCert: String =
      File("../envoy/test/config/integration/certs/upstreamcacert.pem").readText()
    TestJni.initXdsTestServer()
    val latch = CountDownLatch(1)
    engine =
      AndroidEngineBuilder(appContext)
        .addLogLevel(LogLevel.DEBUG)
        .setOnEngineRunning { latch.countDown() }
        .setXds(
          XdsBuilder(
              TestJni.getXdsTestServerHost(),
              TestJni.getXdsTestServerPort(),
            )
            .setSslRootCerts(upstreamCert)
            .addClusterDiscoveryService()
        )
        .build()
    latch.await()
    TestJni.startXdsTestServer()
  }

  @After
  fun tearDown() {
    engine.terminate()
    TestJni.shutdownXdsTestServer()
  }

  @Test
  fun `test xDS with CDS`() {
    // There are 2 initial clusters: base and base_clear.
    engine.waitForStatGe("cluster_manager.cluster_added", 2)
    val cdsResponse =
      """
      version_info: v1
      resources:
      - "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
        name: my_cluster
        type: STATIC
        connect_timeout: 5s
        typed_extension_protocol_options:
          envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
            "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
            explicit_http_config:
              http2_protocol_options:
                {}
      type_url: type.googleapis.com/envoy.config.cluster.v3.Cluster
      nonce: nonce1
    """
        .trimIndent()
    TestJni.sendDiscoveryResponse(cdsResponse)
    // There are now 3 clusters: base, base_cluster, and my_cluster.
    engine.waitForStatGe("cluster_manager.cluster_added", 3)
  }
}
