#include "app/Theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <windows.h>

#include <dwmapi.h>

namespace efs {

namespace {

// 配色。QPalette のロールへ割り当てる元の値をここ 1 箇所に置く。
// Dark は Phase 2 の値をそのまま (見た目を regression させない)。
struct Colors {
    const char* window;
    const char* base;
    const char* alternateBase;
    const char* button;
    const char* text;
    const char* disabledText;
    const char* highlight;
    const char* highlightedText;
    const char* tooltipBase;
    const char* placeholder;
    const char* border;
    const char* checked; // チェック済みツールバーボタンの地
    const char* checkedBorder;
    const char* hover;
    const char* matchBackground; // 検索クエリに一致した部分の地
    const char* matchText;
};

constexpr Colors kDark{
    .window = "#1e1e1e",
    .base = "#181818",
    .alternateBase = "#232323",
    .button = "#2d2d30",
    .text = "#e6e6e6",
    .disabledText = "#7a7a7a",
    .highlight = "#0a84ff",
    .highlightedText = "#ffffff",
    .tooltipBase = "#2d2d30",
    .placeholder = "#8a8a8a",
    .border = "#3c3c3c",
    .checked = "#094771",
    .checkedBorder = "#0a84ff",
    .hover = "#37373d",
    .matchBackground = "#ffd54f",
    .matchText = "#1a1a1a",
};

constexpr Colors kLight{
    .window = "#f3f3f3",
    .base = "#ffffff",
    .alternateBase = "#f7f7f7",
    .button = "#e8e8e8",
    .text = "#1a1a1a",
    .disabledText = "#9a9a9a",
    .highlight = "#0a6ed1",
    .highlightedText = "#ffffff",
    .tooltipBase = "#ffffff",
    .placeholder = "#767676",
    .border = "#c8c8c8",
    .checked = "#cfe3f7",
    .checkedBorder = "#0a6ed1",
    .hover = "#dcdcdc",
    .matchBackground = "#ffe066",
    .matchText = "#1a1a1a",
};

QPalette paletteFor(const Colors& c)
{
    QPalette palette;
    const QColor text(c.text);
    const QColor disabled(c.disabledText);

    palette.setColor(QPalette::Window, QColor(c.window));
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor(c.base));
    palette.setColor(QPalette::AlternateBase, QColor(c.alternateBase));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, QColor(c.placeholder));
    palette.setColor(QPalette::Button, QColor(c.button));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(Qt::red));
    palette.setColor(QPalette::Highlight, QColor(c.highlight));
    palette.setColor(QPalette::HighlightedText, QColor(c.highlightedText));
    palette.setColor(QPalette::ToolTipBase, QColor(c.tooltipBase));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, QColor(c.checkedBorder));
    palette.setColor(QPalette::Light, QColor(c.border));
    palette.setColor(QPalette::Mid, QColor(c.border));
    palette.setColor(QPalette::Dark, QColor(c.base));
    palette.setColor(QPalette::Shadow, QColor(Qt::black));

    // 無効状態。既定のままだと地と文字のコントラストが足りず読めなくなる。
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(c.button));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);

    return palette;
}

// palette だけでは Fusion が十分な差を付けてくれない箇所の補正。
// これ以上 widget を個別指定して肥大させない。
QString detailStyleSheet(const Colors& c)
{
    return QStringLiteral("QToolBar { border: 0px; padding: 2px; spacing: 2px; }"
                          "QToolButton { padding: 3px 8px; border: 1px solid transparent;"
                          " border-radius: 3px; }"
                          "QToolButton:hover { background: %1; }"
                          "QToolButton:checked { background: %2; border: 1px solid %3; }"
                          "QHeaderView::section { background: %4; color: %5; padding: 4px;"
                          " border: 0px; border-right: 1px solid %6; }"
                          "QToolTip { background: %7; color: %5; border: 1px solid %6; }")
        .arg(QLatin1String(c.hover), QLatin1String(c.checked), QLatin1String(c.checkedBorder),
             QLatin1String(c.button), QLatin1String(c.text), QLatin1String(c.border),
             QLatin1String(c.tooltipBase));
}

} // namespace

bool resolveDark(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Dark:
        return true;
    case ThemeMode::Light:
        return false;
    case ThemeMode::System:
        break;
    }
    // OS の配色。Unknown のときは既定 (Dark) 側へ倒す。
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

MatchColors matchColors(const QPalette& palette)
{
    // 明暗は palette の Base (結果テーブルの地) から判定する。テーマ適用は
    // applyTheme() が済ませているので、ここが唯一の入力でよい。
    const Colors& colors = palette.color(QPalette::Base).lightness() < 128 ? kDark : kLight;
    return {QColor(colors.matchBackground), QColor(colors.matchText)};
}

void applyTheme(QApplication& app, ThemeMode mode)
{
    const Colors& colors = resolveDark(mode) ? kDark : kLight;

    // Fusion にするのは、Windows ネイティブスタイルが palette を無視して
    // 明るいまま描く箇所があるため (計画 7)。
    //
    // **既に Fusion なら差し替えない。** setStyle は全 widget を作り直しに近い
    // 再 polish にかけるため、テーマを切り替えるたびに呼ぶとステータスバーの
    // 表示中メッセージが消える (Phase 3 の Light 対応で実際に起きた)。
    // スタイルはテーマに依らず同じなので、初回だけでよい。
    const QStyle* current = QApplication::style();
    if (current == nullptr ||
        current->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QApplication::setPalette(paletteFor(colors));
    // 追記ではなく差し替え。何度切り替えても stylesheet は累積しない。
    app.setStyleSheet(detailStyleSheet(colors));
}

void applyTitleBarTheme(QWidget* window, bool dark)
{
    if (!window)
        return;

    // DWMWA_USE_IMMERSIVE_DARK_MODE は Windows SDK 10.0.22000 以降の dwmapi.h が
    // DWMWINDOWATTRIBUTE として定義している (このリポジトリの対象 SDK は
    // 10.0.26100)。値をハードコードせずシンボルを使う。
    //
    // **best-effort。** この属性の挙動について Microsoft は互換性を保証して
    // おらず、対応しない環境では DwmSetWindowAttribute が失敗してタイトルバーが
    // 明るいまま残るだけ (害は無いので戻り値は見ない)。Windows 11 26200 で
    // 実際に切り替わることは確認済み。
    const BOOL enabled = dark ? TRUE : FALSE;
    // QWidget::winId() は WId (quintptr) を返すので、HWND へ戻すには整数から
    // ポインタへのキャストしか無い。Qt と Win32 の境界そのもの。
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* handle = reinterpret_cast<HWND>(window->winId());
    ::DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
}

} // namespace efs
