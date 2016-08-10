#pragma once

class TestUtility {
public:
  /**
   * Compare 2 buffers.
   * @param lhs supplies buffer 1.
   * @param rhs supplies buffer 2.
   * @return TRUE if the buffers are equal, false if not.
   */
  static bool buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs);

  /**
   * Convert a buffer to a string.
   * @param buffer supplies the buffer to convert.
   * @return std::string the converted string.
   */
  static std::string bufferToString(const Buffer::Instance& buffer);
};
