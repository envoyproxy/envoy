package io.envoyproxy.envoymobile

import android.content.Context
import android.net.ConnectivityManager

class Envoy {

  fun load() {
    System.loadLibrary("envoy_jni")
  }

  fun run(context: Context, config: String) {
    val thread = Thread(Runnable {
      initialize(context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager)
      runEnvoy(config.trim())
    })
    thread.start()
  }

  private external fun initialize(connectivityManager: ConnectivityManager): Int

  private external fun isAresInitialized(): Boolean

  private external fun runEnvoy(config: String): Int
}
