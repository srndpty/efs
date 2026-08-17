#include "app/HotkeySpec.h"

#include <QStringList>
#include <Qt>

#include <array>
#include <utility>

namespace efs {

namespace {

// 修飾キーの綴り。Settings.cpp の enum ↔ 文字列表と同じ形にしてある。
template <std::size_t N>
using ModifierNames = std::array<std::pair<quint32, const char*>, N>;

// 別名も受ける (手編集した INI を素直に読めるように)。
constexpr ModifierNames<6> kModifiers{{
    {HotkeyModControl, "Ctrl"},
    {HotkeyModControl, "Control"},
    {HotkeyModAlt, "Alt"},
    {HotkeyModShift, "Shift"},
    {HotkeyModMeta, "Meta"},
    {HotkeyModMeta, "Win"},
}};

// 出力に使う綴りと順序。
constexpr ModifierNames<4> kModifierOutput{{
    {HotkeyModControl, "Ctrl"},
    {HotkeyModAlt, "Alt"},
    {HotkeyModShift, "Shift"},
    {HotkeyModMeta, "Meta"},
}};

// 対応キー。増やす必要が出るまで広げない。
bool parseKey(const QString& token, quint32& key)
{
    if (token.size() == 1) {
        const QChar c = token.at(0).toUpper();
        if (c >= u'A' && c <= u'Z') {
            key = static_cast<quint32>(Qt::Key_A) + static_cast<quint32>(c.unicode() - u'A');
            return true;
        }
        if (c >= u'0' && c <= u'9') {
            key = static_cast<quint32>(Qt::Key_0) + static_cast<quint32>(c.unicode() - u'0');
            return true;
        }
        return false;
    }

    if (token.compare(QLatin1String("Space"), Qt::CaseInsensitive) == 0) {
        key = static_cast<quint32>(Qt::Key_Space);
        return true;
    }

    // F1〜F24。
    if (token.size() >= 2 && (token.at(0) == u'F' || token.at(0) == u'f')) {
        bool ok = false;
        const int number = token.mid(1).toInt(&ok);
        if (ok && number >= 1 && number <= 24) {
            key = static_cast<quint32>(Qt::Key_F1) + static_cast<quint32>(number - 1);
            return true;
        }
    }
    return false;
}

QString keyText(quint32 key)
{
    if (key >= static_cast<quint32>(Qt::Key_A) && key <= static_cast<quint32>(Qt::Key_Z))
        return {QChar(u'A' + static_cast<char16_t>(key - static_cast<quint32>(Qt::Key_A)))};
    if (key >= static_cast<quint32>(Qt::Key_0) && key <= static_cast<quint32>(Qt::Key_9))
        return {QChar(u'0' + static_cast<char16_t>(key - static_cast<quint32>(Qt::Key_0)))};
    if (key == static_cast<quint32>(Qt::Key_Space))
        return QStringLiteral("Space");
    if (key >= static_cast<quint32>(Qt::Key_F1) && key <= static_cast<quint32>(Qt::Key_F1) + 23)
        return QStringLiteral("F%1").arg(key - static_cast<quint32>(Qt::Key_F1) + 1);
    return {};
}

} // namespace

HotkeySpec parseHotkey(const QString& text)
{
    HotkeySpec spec;
    if (text.trimmed().isEmpty())
        return spec; // 空 = ホットキー無効。invalid として返る

    // **空の項を捨てない。** `Qt::SkipEmptyParts` で読み飛ばすと
    // `Ctrl++Alt+E` や `+Ctrl+Alt+E` が正しい綴りと同じに解釈されてしまい、
    // 「打ち間違えた INI が黙って動く」ことになる。壊れた綴りは invalid にする。
    const QStringList tokens = text.split(u'+');
    bool keySeen = false;

    for (const QString& raw : tokens) {
        const QString token = raw.trimmed();
        if (token.isEmpty())
            return {};

        bool isModifier = false;
        for (const auto& [flag, name] : kModifiers) {
            if (token.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
                // 同じ修飾キーを 2 度書いた指定 (`Ctrl+Ctrl+E` / `Ctrl+Control+E`)
                // は打ち間違い。黙って 1 つに畳むと、壊れた INI が正しい綴りと
                // 同じに解釈される (空の項を捨てないのと同じ理由)。
                if ((spec.modifiers & flag) != 0)
                    return {};
                spec.modifiers |= flag;
                isModifier = true;
                break;
            }
        }
        if (isModifier)
            continue;

        // キーは 1 つだけ。2 つ目が来たら解釈できない入力として捨てる。
        if (keySeen)
            return {};
        if (!parseKey(token, spec.key))
            return {};
        keySeen = true;
    }

    // 修飾なし / キーなしは受け付けない (isValid が false になる)。
    if (!spec.isValid())
        return {};
    return spec;
}

QString formatHotkey(const HotkeySpec& spec)
{
    if (!spec.isValid())
        return {};

    const QString key = keyText(spec.key);
    if (key.isEmpty())
        return {};

    QStringList parts;
    for (const auto& [flag, name] : kModifierOutput) {
        if ((spec.modifiers & flag) != 0)
            parts << QString::fromLatin1(name);
    }
    parts << key;
    return parts.join(u'+');
}

QString defaultHotkeyText()
{
    return QStringLiteral("Ctrl+Alt+E");
}

} // namespace efs
