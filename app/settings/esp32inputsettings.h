#pragma once

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace Esp32InputSettings {
QVariantMap load(const QString& hostUuid);
// Empty return value means saved successfully. Errors never include secrets.
QString save(const QString& hostUuid, const QVariantMap& values);
void remove(const QString& hostUuid);
QUrl endpoint(const QVariantMap& values);
QString canonicalKeyCode(const QString& token);
}
