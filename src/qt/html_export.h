#ifndef M3_QT_HTML_EXPORT_H
#define M3_QT_HTML_EXPORT_H
#include <QByteArray>
#include <QImage>
#include <QString>

namespace m3::qt {
QString encodeHtml(const QByteArray &document, const QImage &mapImage);
}
#endif
