package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;
import java.nio.charset.StandardCharsets;

/**
 * JNI compatibility class to translate calls to EnvoyKeyValueStore implementations.
 *
 * Dealing with Java Strings directly in the JNI is awkward due to how Java encodes them.
 */
class JvmKeyValueStoreContext {
  private static final byte[] EMPTY_BYTES = {};
  private final EnvoyKeyValueStore keyValueStore;

  public JvmKeyValueStoreContext(EnvoyKeyValueStore keyValueStore) {
    this.keyValueStore = keyValueStore;
  }

  public byte[] read(byte[] key) {
    final String value = keyValueStore.read(new String(key, StandardCharsets.UTF_8));
    if (value == null) {
      return EMPTY_BYTES;
    }
    return value.getBytes(StandardCharsets.UTF_8);
  }

  public void remove(byte[] key) { keyValueStore.remove(new String(key, StandardCharsets.UTF_8)); }

  public void save(byte[] key, byte[] value) {
    keyValueStore.save(new String(key, StandardCharsets.UTF_8),
                       new String(value, StandardCharsets.UTF_8));
  }
}
