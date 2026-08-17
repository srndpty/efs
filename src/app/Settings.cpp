#include "app/Settings.h"

#include <QSettings>
#include <QString>

#include <array>

namespace efs {

namespace {

// INI 上のキー。文字列を 2 箇所 (load/save) に散らさないため定数にする。
constexpr auto kVersionKey = "settingsVersion";
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kGeometryKey = "window/geometry";
constexpr auto kWindowStateKey = "window/state";
constexpr auto kHeaderStateKey = "window/headerState";
constexpr auto kKindKey = "search/kind";
constexpr auto kRegexKey = "search/regex";
constexpr auto kSortKeyKey = "search/sortKey";
constexpr auto kSortOrderKey = "search/sortOrder";
constexpr auto kHotkeyKey = "hotkey/show";

// 現行スキーマ。番号が違う INI は「読めない」とみなして全項目を既定値に戻す。
// 汎用の migration framework は作らない (今のところ移行元が存在しない)。
constexpr int kSettingsVersion = 1;

// enum ↔ 安定した文字列。int の生値で書くと、列挙子の順序を入れ替えたときに
// 既存の INI が黙って別の意味になる。
template <typename Enum, std::size_t N>
using EnumNames = std::array<std::pair<Enum, const char*>, N>;

constexpr EnumNames<ThemeMode, 3> kThemeNames{{
    {ThemeMode::System, "system"},
    {ThemeMode::Dark, "dark"},
    {ThemeMode::Light, "light"},
}};

constexpr EnumNames<FileKind, 6> kKindNames{{
    {FileKind::All, "all"},
    {FileKind::Image, "image"},
    {FileKind::Video, "video"},
    {FileKind::Audio, "audio"},
    {FileKind::Document, "document"},
    {FileKind::Directory, "directory"},
}};

constexpr EnumNames<SortKey, 4> kSortKeyNames{{
    {SortKey::Name, "name"},
    {SortKey::Path, "path"},
    {SortKey::Size, "size"},
    {SortKey::DateModified, "dateModified"},
}};

constexpr EnumNames<SortOrder, 2> kSortOrderNames{{
    {SortOrder::Asc, "asc"},
    {SortOrder::Desc, "desc"},
}};

template <typename Enum, std::size_t N>
QString nameOf(const EnumNames<Enum, N>& names, Enum value)
{
    for (const auto& [key, name] : names) {
        if (key == value)
            return QString::fromLatin1(name);
    }
    return {};
}

// 未知の値・型の合わない値・欠落は fallback へ。ここが「壊れた設定でも安全な
// デフォルトへ戻る」の実体。
template <typename Enum, std::size_t N>
Enum readEnum(const QSettings& settings, const char* key, const EnumNames<Enum, N>& names,
              Enum fallback)
{
    const QString stored = settings.value(QString::fromLatin1(key)).toString();
    if (stored.isEmpty())
        return fallback;
    for (const auto& [value, name] : names) {
        if (stored.compare(QString::fromLatin1(name), Qt::CaseInsensitive) == 0)
            return value;
    }
    return fallback;
}

// QByteArray 以外が入っていた場合も落とさず空を返す (壊れた INI の防御)。
QByteArray readByteArray(const QSettings& settings, const char* key)
{
    const QVariant value = settings.value(QString::fromLatin1(key));
    if (!value.canConvert<QByteArray>())
        return {};
    return value.toByteArray();
}

bool readBool(const QSettings& settings, const char* key, bool fallback)
{
    const QVariant value = settings.value(QString::fromLatin1(key));
    if (!value.isValid())
        return fallback;
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool();

    // INI からは文字列として戻る。QVariant::toBool() をそのまま使ってはいけない
    // — 空でない文字列は何でも true になるため、"maybe" のような壊れた値が
    // 黙って Regex ON になってしまう。既知の綴りだけを受け入れる。
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1"))
        return true;
    if (text == QLatin1String("false") || text == QLatin1String("0"))
        return false;
    return fallback;
}

} // namespace

Settings Settings::load()
{
    // const にすると return defaults; で自動 move が効かない。
    Settings defaults;
    QSettings settings;

    // 保存されていない (初回起動) / 別スキーマなら丸ごと既定値。
    const QVariant version = settings.value(QString::fromLatin1(kVersionKey));
    if (!version.isValid() || version.toInt() != kSettingsVersion)
        return defaults;

    Settings loaded;
    loaded.theme = readEnum(settings, kThemeKey, kThemeNames, defaults.theme);
    loaded.options.kind = readEnum(settings, kKindKey, kKindNames, defaults.options.kind);
    loaded.options.regex = readBool(settings, kRegexKey, defaults.options.regex);
    loaded.options.sortKey =
        readEnum(settings, kSortKeyKey, kSortKeyNames, defaults.options.sortKey);
    loaded.options.sortOrder =
        readEnum(settings, kSortOrderKey, kSortOrderNames, defaults.options.sortOrder);

    // ホットキーは文字列のまま持つが、解釈できない綴りは既定へ戻す
    // (壊れた INI で「ホットキーが黙って無効」になるのを避ける)。空文字は
    // 「意図的に無効」なので尊重する。
    if (settings.contains(QString::fromLatin1(kHotkeyKey))) {
        const QString stored = settings.value(QString::fromLatin1(kHotkeyKey)).toString();
        if (stored.trimmed().isEmpty())
            loaded.hotkey.clear();
        else
            loaded.hotkey = parseHotkey(stored).isValid() ? stored : defaults.hotkey;
    }

    loaded.windowGeometry = readByteArray(settings, kGeometryKey);
    loaded.windowState = readByteArray(settings, kWindowStateKey);
    loaded.headerState = readByteArray(settings, kHeaderStateKey);

    return loaded;
}

void Settings::save() const
{
    QSettings settings;

    settings.setValue(QString::fromLatin1(kVersionKey), kSettingsVersion);
    settings.setValue(QString::fromLatin1(kThemeKey), nameOf(kThemeNames, theme));
    settings.setValue(QString::fromLatin1(kKindKey), nameOf(kKindNames, options.kind));
    settings.setValue(QString::fromLatin1(kRegexKey), options.regex);
    settings.setValue(QString::fromLatin1(kSortKeyKey), nameOf(kSortKeyNames, options.sortKey));
    settings.setValue(QString::fromLatin1(kSortOrderKey),
                      nameOf(kSortOrderNames, options.sortOrder));

    settings.setValue(QString::fromLatin1(kHotkeyKey), hotkey);

    settings.setValue(QString::fromLatin1(kGeometryKey), windowGeometry);
    settings.setValue(QString::fromLatin1(kWindowStateKey), windowState);
    settings.setValue(QString::fromLatin1(kHeaderStateKey), headerState);

    // 検索文字列・検索履歴・最近の query・結果行は**保存しない**。
    // 検索文字列の永続化は search history と意味が混ざるため Phase 3 では行わない。
}

} // namespace efs
