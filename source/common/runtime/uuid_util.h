#pragma once

/*
 * Utils for uuid4.
 */
class UuidUtils {
public:
  /**
   * @return bool to indicate if operation succeeded.
   * @param uuid uuid4.
   * @param out will contain the result of the operation.
   * @param mod modulo used in the operation.
   */
  static bool uuidModBy(const std::string& uuid, uint16_t& out, uint16_t mod);

  /**
   * Modify uuid in a way it can be detected if uuid is traceable or not.
   * @param uuid is expected to be well formed uuid4.
   * @return true on success, false on failure.
   */
  static bool setTraceableUuid(std::string& uuid);

  /**
   * @return bool to indicate if @param uuid is traceable uuid4.
   */
  static bool isTraceableUuid(const std::string& uuid);

private:
  // Byte on this position has predefined value of 4 for UUID4
  static const int TRACE_BYTE_POSITION = 14;
  // Value of 9 is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified so that we can detect if it's traceable.
  static const char TRACE_BYTE_VALUE = '9';
};
