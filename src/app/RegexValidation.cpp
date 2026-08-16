#include "app/RegexValidation.h"

#include <QRegularExpression>

namespace efs {

RegexValidation validateRegex(const QString& pattern, bool regexEnabled)
{
    if (!regexEnabled || pattern.isEmpty())
        return {};

    // pattern はここでコピーもトリムもしない。QRegularExpression へ渡すだけ。
    const QRegularExpression re(pattern);
    if (re.isValid())
        return {.checked = true, .valid = true, .errorString = {}, .errorOffset = -1};

    return {.checked = true,
            .valid = false,
            .errorString = re.errorString(),
            .errorOffset = re.patternErrorOffset()};
}

} // namespace efs
