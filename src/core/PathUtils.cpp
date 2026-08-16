#include "core/PathUtils.h"

namespace efs {

namespace {

constexpr QChar kSeparator = u'\\';

bool endsWithSeparator(const QString& path)
{
    return path.endsWith(u'\\') || path.endsWith(u'/');
}

// "C:" のようなドライブ指定 1 つだけか。
bool isBareDrive(const QString& name)
{
    return name.size() == 2 && name.at(1) == u':' && name.at(0).isLetter();
}

} // namespace

QString fullPath(const ResultRow& row)
{
    if (row.name.isEmpty())
        return row.path;

    if (row.path.isEmpty()) {
        // ドライブそのもの。裸の "C:" は相対パスなので必ず区切りを付ける。
        if (isBareDrive(row.name))
            return row.name + kSeparator;
        return row.name;
    }

    if (endsWithSeparator(row.path))
        return row.path + row.name;

    return row.path + kSeparator + row.name;
}

} // namespace efs
