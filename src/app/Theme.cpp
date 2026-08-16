#include "app/Theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QWidget>

#include <windows.h>

#include <dwmapi.h>

namespace efs {

namespace {

// 配色。QPalette のロールへ割り当てる元の値をここ 1 箇所に置く。
constexpr auto kWindow = "#1e1e1e";        // MainWindow / toolbar の地
constexpr auto kBase = "#181818";          // 検索欄・テーブルの地
constexpr auto kAlternateBase = "#232323"; // 交互行
constexpr auto kButton = "#2d2d30";        // ボタン・ヘッダ
constexpr auto kText = "#e6e6e6";
constexpr auto kDisabledText = "#7a7a7a";
constexpr auto kHighlight = "#0a84ff"; // 選択行
constexpr auto kHighlightedText = "#ffffff";
constexpr auto kTooltipBase = "#2d2d30";
constexpr auto kPlaceholder = "#8a8a8a";
constexpr auto kBorder = "#3c3c3c";
constexpr auto kChecked = "#094771"; // チェック済みツールバーボタン
constexpr auto kCheckedBorder = "#0a84ff";
constexpr auto kHover = "#37373d";

QPalette darkPalette()
{
    QPalette palette;
    const QColor text(kText);
    const QColor disabled(kDisabledText);

    palette.setColor(QPalette::Window, QColor(kWindow));
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor(kBase));
    palette.setColor(QPalette::AlternateBase, QColor(kAlternateBase));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, QColor(kPlaceholder));
    palette.setColor(QPalette::Button, QColor(kButton));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(Qt::red));
    palette.setColor(QPalette::Highlight, QColor(kHighlight));
    palette.setColor(QPalette::HighlightedText, QColor(kHighlightedText));
    palette.setColor(QPalette::ToolTipBase, QColor(kTooltipBase));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, QColor(kCheckedBorder));
    palette.setColor(QPalette::Light, QColor(kBorder));
    palette.setColor(QPalette::Mid, QColor(kBorder));
    palette.setColor(QPalette::Dark, QColor(kBase));
    palette.setColor(QPalette::Shadow, QColor(Qt::black));

    // 無効状態。既定のままだと明るい地に明るい文字が乗って読めなくなる。
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(kButton));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);

    return palette;
}

// palette だけでは Fusion が十分な差を付けてくれない箇所の補正。
// これ以上 widget を個別指定して肥大させない。
QString detailStyleSheet()
{
    return QStringLiteral("QToolBar { border: 0px; padding: 2px; spacing: 2px; }"
                          "QToolButton { padding: 3px 8px; border: 1px solid transparent;"
                          " border-radius: 3px; }"
                          "QToolButton:hover { background: %1; }"
                          "QToolButton:checked { background: %2; border: 1px solid %3; }"
                          "QHeaderView::section { background: %4; color: %5; padding: 4px;"
                          " border: 0px; border-right: 1px solid %6; }"
                          "QToolTip { background: %7; color: %5; border: 1px solid %6; }")
        .arg(QLatin1String(kHover), QLatin1String(kChecked), QLatin1String(kCheckedBorder),
             QLatin1String(kButton), QLatin1String(kText), QLatin1String(kBorder),
             QLatin1String(kTooltipBase));
}

} // namespace

void applyDarkTheme(QApplication& app)
{
    // Fusion にするのは、Windows ネイティブスタイルが palette を無視して
    // 明るいまま描く箇所があるため (計画 7)。
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(darkPalette());
    app.setStyleSheet(detailStyleSheet());
}

void applyDarkTitleBar(QWidget* window)
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
    // 実際に暗くなることは確認済み。それ以上の強制 Dark 化は Phase 3 まで行わない。
    const BOOL enabled = TRUE;
    // QWidget::winId() は WId (quintptr) を返すので、HWND へ戻すには整数から
    // ポインタへのキャストしか無い。Qt と Win32 の境界そのもの。
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* handle = reinterpret_cast<HWND>(window->winId());
    ::DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
}

} // namespace efs
