package io.envoyproxy.envoymobile.engine.types;

public interface EnvoyKeyValueStore {
  /**
   * Read a value from the key value store implementation.
   *
   * @param key,     key identifying the value to be returned.
   * @return String, value mapped to the key, or null if not present.
   */
  String read(String key);

  /**
   * Remove a value from the key value store implementation.
   *
   * @param key,     key identifying the value to be removed.
   */
  void remove(String key);

  /**
   * Save a value to the key value store implementation.
   *
   * @param key,     key identifying the value to be saved.
   * @param value,   the value to be saved.
   */
  void save(String key, String value);
}
