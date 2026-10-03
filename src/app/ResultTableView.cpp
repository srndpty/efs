#include "app/ResultTableView.h"

#include "app/ResultTableModel.h"
#include "app/ShellDrag.h"

#include <QApplication>
#include <QModelIndex>
#include <QMouseEvent>

namespace efs {

ResultTableView::ResultTableView(QWidget* parent) : QTableView(parent) {}

void ResultTableView::mousePressEvent(QMouseEvent* event)
{
    // 先に基底へ渡して選択と currentIndex を確定させる。右クリックのメニューも
    // 従来どおり基底の経路で出る。
    QTableView::mousePressEvent(event);

    m_pressPath.clear();
    if (event->button() != Qt::LeftButton)
        return;

    m_pressPos = event->position().toPoint();
    const QModelIndex index = indexAt(m_pressPos);
    if (index.isValid())
        m_pressPath = index.data(ResultTableModel::FullPathRole).toString();
}

void ResultTableView::mouseMoveEvent(QMouseEvent* event)
{
    // **入れ子のループからの再入はここで捨てる。** SHDoDragDrop の間も Qt の
    // イベントは配送されるので、基底へ渡すと掴んでいる最中の move が範囲選択の
    // ドラッグとして解釈される。
    if (m_dragging) {
        event->accept();
        return;
    }

    const bool canStart = !m_pressPath.isEmpty() && (event->buttons() & Qt::LeftButton) != 0 &&
                          (event->position().toPoint() - m_pressPos).manhattanLength() >=
                              QApplication::startDragDistance();
    if (!canStart) {
        QTableView::mouseMoveEvent(event);
        return;
    }

    // ここで掴む対象を確定させ、状態は先に落とす。startShellDrag() は
    // ドロップまで戻らず、その間に Qt のイベントも回るため。
    const QString path = m_pressPath;
    m_pressPath.clear();
    m_dragging = true;
    static_cast<void>(startShellDrag(this, path));
    m_dragging = false;
    // ボタンを離したのは入れ子のループの中なので release は届かない。
    // 基底へは渡さず (渡すと範囲選択のドラッグとして解釈される) ここで終える。
}

void ResultTableView::mouseReleaseEvent(QMouseEvent* event)
{
    m_pressPath.clear();
    QTableView::mouseReleaseEvent(event);
}

} // namespace efs
