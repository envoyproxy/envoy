package io.envoyproxy.envoymobile

import android.app.Application
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

class AndroidEnvoyClientBuilder @JvmOverloads constructor (
  application: Application,
  baseConfiguration: BaseConfiguration = Standard()
) : EnvoyClientBuilder(baseConfiguration) {
  init {
    addEngineType { AndroidEngineImpl(application) }
  }
}
