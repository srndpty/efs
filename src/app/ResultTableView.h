// 結果テーブルの view (Phase 6 / D&D)。
//
// QTableView との違いはドラッグの開始だけ。Qt の QDrag は使わず、シェルの
// データオブジェクトで DoDragDrop する (理由は app/ShellDrag.h)。そのため
// setDragEnabled() / startDrag() の経路には乗らず、マウス操作から直接始める。
#pragma once

#include <QPoint>
#include <QString>
#include <QTableView>

namespace efs {

class ResultTableView : public QTableView {
    Q_OBJECT

public:
    explicit ResultTableView(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // 押した位置と、そのとき下にあった行のフルパス。
    // **QModelIndex ではなく文字列で持つ。** ドラッグ中もイベントは配送される
    // ので、掴んでいる間に検索結果が届いてモデルが reset されうる。index を
    // 持つと別の行 (あるいは消えた行) を指したまま drop されることになる。
    QPoint m_pressPos;
    QString m_pressPath;
    // DoDragDrop の入れ子のループから mouseMoveEvent が再入するのを防ぐ。
    bool m_dragging = false;
};

} // namespace efs
