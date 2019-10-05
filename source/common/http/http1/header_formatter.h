#pragma once

#include "absl/strings/string_view.h"
#include <cctype>
#include <memory>

namespace Envoy {
namespace Http {
namespace Http1 {
class HeaderKeyFormatter {
public:
    virtual ~HeaderKeyFormatter() = default;

    virtual std::string format(absl::string_view key) PURE;
};

using HeaderKeyFormatterPtr = std::unique_ptr<HeaderKeyFormatter>;

/**
* A HeaderKeyFormatter that converts each key into Train-Case: The first characeter
* as well as any alpha characeter following a special character is uppercased.
*/
class TrainCaseHeaderKeyFormatter : public HeaderKeyFormatter {
    std::string format(absl::string_view key) override {
        auto copy = std::string(key);

        bool shouldCapitalize = true;
        for (size_t i = 0; i < copy.size(); ++i) {
            if (shouldCapitalize && isalpha(copy[i])) {
                copy[i] = toupper(copy[i]);
            }

            shouldCapitalize = !isalpha(copy[i]) && !isdigit(copy[i]);
        }

        return copy;
    }
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
