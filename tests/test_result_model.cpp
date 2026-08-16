// ResultTableModel の単体テスト。QAbstractItemModelTester でモデル契約を検証し、
// 表示内容は DisplayRole で確認する。Everything に依存しないため常に実行する。
#include <QAbstractItemModelTester>
#include <QLocale>
#include <QSignalSpy>
#include <QtTest>

#include "app/ResultTableModel.h"
#include "core/Formatting.h"

using efs::ResultRow;
using efs::ResultTableModel;

class TestResultModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyModel();
    void rowAndColumnCount();
    void displayRoleShowsRowContents();
    void sizeColumnIsEmptyForDirectories();
    void sizeColumnIsRightAligned();
    void invalidDateIsShownAsDash();
    void headerLabels();
    void setRowsResetsModel();
    void invalidIndexReturnsNothing();

private:
    static QVector<ResultRow> sampleRows();
};

void TestResultModel::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

QVector<ResultRow> TestResultModel::sampleRows()
{
    ResultRow file;
    file.name = QStringLiteral("report.pdf");
    file.path = QStringLiteral("C:\\docs");
    file.size = 2048;
    file.modified = QDateTime(QDate(2026, 8, 16), QTime(9, 30));
    file.isDir = false;

    ResultRow directory;
    directory.name = QStringLiteral("docs");
    directory.path = QStringLiteral("C:\\");
    directory.size = -1;
    directory.isDir = true;

    return {file, directory};
}

void TestResultModel::emptyModel()
{
    ResultTableModel model;
    // 既定の FailureReportingMode::QtTest。契約違反はテスト失敗として報告される。
    const QAbstractItemModelTester tester(&model);

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), ResultTableModel::ColumnCount);
    QVERIFY(!model.index(0, 0).isValid());
}

void TestResultModel::rowAndColumnCount()
{
    ResultTableModel model;
    // 既定の FailureReportingMode::QtTest。契約違反はテスト失敗として報告される。
    const QAbstractItemModelTester tester(&model);
    model.setRows(sampleRows());

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), 4);
    // テーブルモデルなので、行は子を持たない。
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.columnCount(model.index(0, 0)), 0);
}

void TestResultModel::displayRoleShowsRowContents()
{
    ResultTableModel model;
    model.setRows(sampleRows());

    QCOMPARE(model.index(0, ResultTableModel::ColumnName).data().toString(),
             QStringLiteral("report.pdf"));
    QCOMPARE(model.index(0, ResultTableModel::ColumnPath).data().toString(),
             QStringLiteral("C:\\docs"));
    QCOMPARE(model.index(0, ResultTableModel::ColumnSize).data().toString(), efs::formatSize(2048));
    QCOMPARE(model.index(0, ResultTableModel::ColumnDateModified).data().toString(),
             efs::formatModified(QDateTime(QDate(2026, 8, 16), QTime(9, 30))));
}

void TestResultModel::sizeColumnIsEmptyForDirectories()
{
    ResultTableModel model;
    model.setRows(sampleRows());

    QVERIFY(model.index(1, ResultTableModel::ColumnSize).data().toString().isEmpty());
}

void TestResultModel::sizeColumnIsRightAligned()
{
    ResultTableModel model;
    model.setRows(sampleRows());

    const QVariant alignment =
        model.index(0, ResultTableModel::ColumnSize).data(Qt::TextAlignmentRole);
    QVERIFY(alignment.isValid());
    QVERIFY(alignment.value<Qt::Alignment>().testFlag(Qt::AlignRight));
    // 他の列は既定の配置のままにする。
    QVERIFY(!model.index(0, ResultTableModel::ColumnName).data(Qt::TextAlignmentRole).isValid());
}

void TestResultModel::invalidDateIsShownAsDash()
{
    ResultTableModel model;
    model.setRows(sampleRows());

    QCOMPARE(model.index(1, ResultTableModel::ColumnDateModified).data().toString(),
             QStringLiteral("-"));
}

void TestResultModel::headerLabels()
{
    ResultTableModel model;

    QCOMPARE(model.headerData(ResultTableModel::ColumnName, Qt::Horizontal).toString(),
             QStringLiteral("Name"));
    QCOMPARE(model.headerData(ResultTableModel::ColumnPath, Qt::Horizontal).toString(),
             QStringLiteral("Path"));
    QCOMPARE(model.headerData(ResultTableModel::ColumnSize, Qt::Horizontal).toString(),
             QStringLiteral("Size"));
    QCOMPARE(model.headerData(ResultTableModel::ColumnDateModified, Qt::Horizontal).toString(),
             QStringLiteral("Date Modified"));
    QVERIFY(!model.headerData(0, Qt::Vertical).isValid());
}

void TestResultModel::setRowsResetsModel()
{
    ResultTableModel model;
    // 既定の FailureReportingMode::QtTest。契約違反はテスト失敗として報告される。
    const QAbstractItemModelTester tester(&model);
    model.setRows(sampleRows());

    QSignalSpy aboutToReset(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

    model.setRows({});

    QCOMPARE(aboutToReset.count(), 1);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.rowCount(), 0);
}

void TestResultModel::invalidIndexReturnsNothing()
{
    ResultTableModel model;
    model.setRows(sampleRows());

    QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
    // 未対応のロールは無効な QVariant を返す。
    QVERIFY(!model.index(0, 0).data(Qt::DecorationRole).isValid());
    QVERIFY(!model.index(0, 0).data(Qt::ToolTipRole).isValid());
}

QTEST_GUILESS_MAIN(TestResultModel)
#include "test_result_model.moc"
