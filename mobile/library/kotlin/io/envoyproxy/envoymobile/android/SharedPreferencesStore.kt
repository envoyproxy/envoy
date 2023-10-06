package io.envoyproxy.envoymobile.android

import android.content.SharedPreferences

import io.envoyproxy.envoymobile.KeyValueStore

/**
 * Simple implementation of a `KeyValueStore` leveraging `SharedPreferences` for persistence.
 */
class SharedPreferencesStore(sharedPreferences: SharedPreferences) : KeyValueStore {
  private val preferences = sharedPreferences
  private val editor = sharedPreferences.edit()

  override fun read(key: String): String? {
    return preferences.getString(key, null)
  }

  override fun remove(key: String) {
    editor.remove(key)
    editor.apply()
  }

  override fun save(key: String, value: String) {
    editor.putString(key, value)
    editor.apply()
  }
}
