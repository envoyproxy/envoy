#pragma once

enum class TraceReason { Sampled, Client, Forced };

struct TraceDecision {
  bool is_traced;
  Optional<TraceReason> reason;
};

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
   * @param trace_reason is to specify why we modify uuid.
   * @return true on success, false on failure.
   */
  static bool setTraceableUuid(std::string& uuid, TraceReason trace_reason);

  /**
   * @return bool to indicate if @param uuid is traceable uuid4.
   */
  static TraceDecision isTraceableUuid(const std::string& uuid);

private:
  // Byte on this position has predefined value of 4 for UUID4.
  static const int TRACE_BYTE_POSITION = 14;

  // Value of '9' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we sample trace.
  static const char TRACE_SAMPLED = '9';

  // Value of 'a' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we force trace.
  static const char TRACE_FORCED = 'a';

  // Value of 'a' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because of client trace.
  static const char TRACE_CLIENT = 'b';
};
