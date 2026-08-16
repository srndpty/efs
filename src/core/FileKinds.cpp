#include "core/FileKinds.h"

namespace efs {

QStringList extensionsFor(FileKind kind)
{
    switch (kind) {
    case FileKind::All:
        return {};
    case FileKind::Image:
        return {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                QStringLiteral("gif"), QStringLiteral("bmp"),  QStringLiteral("webp"),
                QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"),
                QStringLiteral("svg"), QStringLiteral("ico"),  QStringLiteral("cr2"),
                QStringLiteral("nef")};
    case FileKind::Video:
        return {QStringLiteral("mp4"),  QStringLiteral("mkv"), QStringLiteral("avi"),
                QStringLiteral("mov"),  QStringLiteral("wmv"), QStringLiteral("flv"),
                QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("mpg"),
                QStringLiteral("mpeg"), QStringLiteral("ts")};
    case FileKind::Audio:
        return {QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("wav"),
                QStringLiteral("aac"),  QStringLiteral("m4a"),  QStringLiteral("ogg"),
                QStringLiteral("opus"), QStringLiteral("wma")};
    case FileKind::Document:
        return {QStringLiteral("pdf"),  QStringLiteral("doc"),  QStringLiteral("docx"),
                QStringLiteral("xls"),  QStringLiteral("xlsx"), QStringLiteral("ppt"),
                QStringLiteral("pptx"), QStringLiteral("txt"),  QStringLiteral("md"),
                QStringLiteral("rtf"),  QStringLiteral("odt"),  QStringLiteral("ods"),
                QStringLiteral("csv"),  QStringLiteral("epub")};
    case FileKind::Directory:
        return {};
    }
    return {};
}

} // namespace efs
