package io.envoyproxy.envoymobile

import java.util.regex.Pattern

/**
 * Element represents one dot-delimited component of a time series name.
 *
 * Element values must conform to the [Element.ELEMENT_REGEX].
 */
class Element(internal val value: String) {
  init {
    require(ELEMENT_PATTERN.matcher(value).matches()) {
      "Element values must conform to the regex $ELEMENT_REGEX"
    }
  }

  companion object {
    private const val ELEMENT_REGEX = "^[A-Za-z_]+$"
    private val ELEMENT_PATTERN = Pattern.compile(ELEMENT_REGEX)
  }
}
