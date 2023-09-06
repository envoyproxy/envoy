package io.envoyproxy.envoymobile.utilities;

import java.util.HashMap;
import java.util.Map;

/**
 * Class for utilities around Envoy stats.
 */
public class StatsUtils {

  /**
   * Takes in a stats string from engine.dumpStats() and parses it into individual map entries.
   * @param statsString, the string from dumpStats
   * @return a map of stats entries, e.g. "runtime.load_success", "1"
   */
  public static Map<String, String> statsToList(String statsString) {
    Map<String, String> stats = new HashMap<>();

    for (String line : statsString.split("\n")) {
      String[] keyValue = line.split(": ");
      if (keyValue.length != 2) {
        System.out.println("Unexpected stats token");
        continue;
      }
      stats.put(keyValue[0], keyValue[1]);
    }
    return stats;
  }
}
