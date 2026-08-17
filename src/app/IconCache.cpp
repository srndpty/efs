#include "app/IconCache.h"

#include <QThread>

#include <utility>

namespace efs {

namespace {

constexpr auto kDirectoryKey = "dir:";
constexpr auto kExtensionPrefix = "ext:";

// worker thread 側。loader を呼んで結果を値でコピーして返すだけ。
// 新しい汎用 thread-pool は作らない — アイコン専用のスレッド 1 本で足りる。
class IconLoadWorker : public QObject {
    Q_OBJECT

public:
    explicit IconLoadWorker(IconLoader loader) : m_loader(std::move(loader)) {}

    // slot。moc が生成する定義とシグネチャ名を揃えるため引数名は書かない
    // (readability-inconsistent-declaration-parameter-name)。
    void load(const QString& key)
    {
        QImage image;
        if (m_loader)
            image = m_loader(parseIconKey(key));
        // QImage は暗黙共有だが、shell から作った時点で独立したデータを持つ。
        emit loaded(key, image);
    }

signals:
    // moc の生成する定義は引数を _t1 / _t2 と名付ける。この worker は .cpp 内に
    // 宣言があり .moc を同じ TU へ取り込むため、clang-tidy には「宣言と定義で
    // 引数名が違う」と見える。名前を消すと今度は readability-named-parameter に
    // 引っかかるので、読みやすさを優先して名前を残しこの検査だけ黙らせる。
    // NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
    void loaded(const QString& key, const QImage& image);

private:
    IconLoader m_loader;
};

} // namespace

QString iconKeyFor(bool isDirectory, const QString& name)
{
    if (isDirectory)
        return QString::fromLatin1(kDirectoryKey);

    // 最後のドットより後ろが拡張子。"archive.tar.gz" → "gz"、"README" と
    // "name." → 空。Windows と同じく ".gitignore" は拡張子 "gitignore" とみなす。
    const qsizetype dot = name.lastIndexOf(u'.');
    const QString extension = dot < 0 ? QString() : name.mid(dot + 1).toLower();
    return QString::fromLatin1(kExtensionPrefix) + extension;
}

IconKey parseIconKey(const QString& key)
{
    if (key == QLatin1String(kDirectoryKey))
        return {.isDirectory = true, .extension = {}};
    const QLatin1String extensionPrefix(kExtensionPrefix);
    if (key.startsWith(extensionPrefix))
        return {.isDirectory = false, .extension = key.mid(extensionPrefix.size())};
    return {};
}

IconCache::IconCache(IconLoader loader, QObject* parent) : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("icons"));

    auto* worker = new IconLoadWorker(std::move(loader));
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    connect(this, &IconCache::loadRequested, worker, &IconLoadWorker::load);
    connect(worker, &IconLoadWorker::loaded, this, &IconCache::onLoaded);

    m_thread->start();
}

IconCache::~IconCache()
{
    m_thread->quit();
    m_thread->wait();
}

bool IconCache::image(const QString& key, QImage* out)
{
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) {
        if (out)
            *out = it.value();
        return true;
    }

    // 同じキーの要求を重ねない。5,000 行が全部 .txt でも lookup は 1 回。
    if (!m_pending.contains(key)) {
        m_pending.insert(key);
        emit loadRequested(key);
    }
    return false;
}

void IconCache::onLoaded(const QString& key, const QImage& image)
{
    m_pending.remove(key);
    // 失敗 (null 画像) もそのまま入れる。入れないと同じ未知拡張子を毎回
    // lookup し直すことになる。
    m_cache.insert(key, image);
    emit imagesReady();
}

} // namespace efs

#include "IconCache.moc"
