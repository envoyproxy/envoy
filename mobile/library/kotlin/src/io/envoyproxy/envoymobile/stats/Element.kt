package io.envoyproxy.envoymobile

import java.util.regex.Pattern

/**
 * Element represents one dot-delimited component of a time series name.
 *
 * Element values must conform to the [Element.ELEMENT_REGEX].
 */

class Element(val element: String) {
  init {
    if (!Pattern.compile(ELEMENT_REGEX).matcher(element).matches()) {
      throw IllegalArgumentException(
        "Element values must conform to the regex $ELEMENT_REGEX"
      )
    }
  }

  companion object {
    private const val ELEMENT_REGEX = "^[A-Za-z_]+$"
  }
}
