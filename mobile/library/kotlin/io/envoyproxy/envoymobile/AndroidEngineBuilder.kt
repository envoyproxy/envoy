package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

/** The engine builder to use to create Envoy engine on Android. */
class AndroidEngineBuilder(context: Context) : EngineBuilder() {
  private var useV2NetworkMonitor = false

  fun setUseV2NetworkMonitor(useV2NetworkMonitor: Boolean): AndroidEngineBuilder {
    this.useV2NetworkMonitor = useV2NetworkMonitor
    return this
  }

  fun setUseQuicPlatformPacketWriter(useQuicPlatformPacketWriter: Boolean): AndroidEngineBuilder {
    this.useQuicPlatformPacketWriter = useQuicPlatformPacketWriter
    return this
  }

  fun setEnableConnectionMigration(enableConnectionMigration: Boolean): AndroidEngineBuilder {
    this.enableConnectionMigration = enableConnectionMigration
    return this
  }

  fun setMigrateIdleConnection(migrateIdleConnection: Boolean): AndroidEngineBuilder {
    this.migrateIdleConnection = migrateIdleConnection
    return this
  }

  fun setMaxIdleTimeBeforeMigrationSeconds(
    maxIdleTimeBeforeMigrationSeconds: Long
  ): AndroidEngineBuilder {
    this.maxIdleTimeBeforeMigrationSeconds = maxIdleTimeBeforeMigrationSeconds
    return this
  }

  fun setMaxTimeOnNonDefaultNetworkSeconds(
    maxTimeOnNonDefaultNetworkSeconds: Long
  ): AndroidEngineBuilder {
    this.maxTimeOnNonDefaultNetworkSeconds = maxTimeOnNonDefaultNetworkSeconds
    return this
  }

  init {
    addEngineType {
      AndroidEngineImpl(
        context,
        onEngineRunning,
        { level, msg -> logger?.let { it(LogLevel.from(level), msg) } },
        eventTracker,
        enableProxying,
        /*useNetworkChangeEvent*/ false,
        /*disableDnsRefreshOnNetworkChange*/ false,
        useV2NetworkMonitor
      )
    }
  }
}
