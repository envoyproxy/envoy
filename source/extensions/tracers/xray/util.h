#pragma once

#include <string>
#include <vector>

#include "envoy/common/time.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Utility class with a few convenient methods.
                */
                class Util {
                public:
                    /**
                     * Merges the stringified JSONs given in target and source.
                     *
                     * @param target It will contain the resulting stringified JSON.
                     * @param source The stringified JSON that will be added to target.
                     * @param field_name The key name (added to target's JSON) whose value will be the JSON in source.
                     */
                    static void mergeJsons(std::string& target, const std::string& source, const std::string& field_name);

                    /**
                     * Merges a stringified JSON and a vector of stringified JSONs.
                     *
                     * @param target It will contain the resulting stringified JSON.
                     * @param json_array Vector of strings, where each element is a stringified JSON.
                     * @param field_name The key name (added to target's JSON) whose value will be a stringified.
                     * JSON array derived from json_array.
                     */
                    static void addArrayToJson(std::string& target, const std::vector<std::string>& json_array, const std::string& field_name);

                    /**
                     * Returns a randomly-generated 64-bit integer number.
                     */
                    static uint64_t generateRandom64(TimeSource& time_source);

                    /**
                     * Generate Random double value.
                     * @return a randomly-generated double number from 0-1.
                     */
                    static double generateRandomDouble();

                    /**
                     * Performs a case-insensitive wildcard match against two strings. This method works with pseduo-regex chars; specifically ? and * are supported.
                     *
                     * An asterisk (*) represents any combination of characters.
                     * A question mark (?) represents any single character.
                     *
                     * @param pattern The regex-like pattern to be compared against.
                     * @param text The string to compare against the pattern.
                     * @return whether the text matches the pattern.
                     */
                    static bool wildcardMatch(std::string& pattern, std::string& text);

                    /**
                     * Performs simple wildcard match.
                     * @param pattern The regex-like pattern to be compared against
                     * @param text The string to compare against the pattern.
                     * @return whether the text matches the pattern.
                     */
                    static bool simpleWildcardMatch(std::string& pattern, std::string& text);

                    /**
                     * Indicates whether the passed pattern is a single wildcard glob
                     * (i.e., "*")
                     */
                    static bool isWildcardGlob(std::string& pattern);

                };
            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
