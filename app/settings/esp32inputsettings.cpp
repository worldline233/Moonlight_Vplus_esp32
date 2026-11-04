#include "esp32inputsettings.h"
#include "../streaming/input/esp32keyboardrouter.h"

#include <QSettings>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QSet>

namespace {
bool validUuid(const QString& uuid)
{
    return !uuid.isEmpty() && !uuid.contains('/') && !uuid.contains('\\');
}
const char* const Fields[] = {
    "enabled", "address", "port", "access_key", "mouse_enabled", "mouse_buttons", "keyboard_enabled",
    "excluded_keys", "shortcut_pause", "shortcut_reconnect", "shortcut_release"};
}

QVariantMap Esp32InputSettings::load(const QString& hostUuid)
{
    QVariantMap values{{"enabled", false}, {"address", QString()}, {"port", 80},
                       {"access_key", QString()}, {"mouse_enabled", true}, {"mouse_buttons", 31}, {"keyboard_enabled", false},
                       {"excluded_keys", QString()}, {"shortcut_pause", 9}, {"shortcut_reconnect", 10}, {"shortcut_release", 11}};
    if (!validUuid(hostUuid)) return values;
    QSettings settings;
    settings.beginGroup(QStringLiteral("ESP32Hosts/" ) + hostUuid);
    for (const auto* field : Fields) values[field] = settings.value(field, values[field]);
    return values;
}

QUrl Esp32InputSettings::endpoint(const QVariantMap& values)
{
    auto address = values.value("address").toString().trimmed();
    if (!address.contains(QStringLiteral("://"))) address.prepend(QStringLiteral("ws://"));
    QUrl url(address, QUrl::StrictMode);
    if (url.scheme() == QStringLiteral("http")) url.setScheme(QStringLiteral("ws"));
    if (url.scheme() == QStringLiteral("https")) url.setScheme(QStringLiteral("wss"));
    const auto key = values.value("access_key").toString();
    bool portOk = false;
    const int port = values.value("port", 80).toInt(&portOk);
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QStringLiteral("ws") && url.scheme() != QStringLiteral("wss")) ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (!url.path().isEmpty() && url.path() != QStringLiteral("/") && url.path() != QStringLiteral("/ws")) ||
        !portOk || values.value("port", 80).toDouble() != port || port < 1 || port > 65535 ||
        (url.port() != -1 && url.port() != port) || key.size() != 6) return {};
    url.setPort(port);
    url.setPath(QStringLiteral("/ws"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    url.setQuery(query);
    return url;
}

QString Esp32InputSettings::save(const QString& hostUuid, const QVariantMap& values)
{
    if (!validUuid(hostUuid)) return QStringLiteral("无法识别这台主机，请重新打开设置。");
    auto merged = load(hostUuid);
    for (const auto* field : Fields) if (values.contains(field)) merged[field] = values[field];
    if (merged.value("enabled").toBool() && endpoint(merged).isEmpty())
        return QStringLiteral("请填写有效的 IP／域名、1–65535 的端口和 6 位控制密钥。地址中的端口须与端口栏一致。");
    bool buttonsOk = false;
    const int buttons = merged.value("mouse_buttons").toInt(&buttonsOk);
    if (!buttonsOk || merged.value("mouse_buttons").toDouble() != buttons || buttons < 0 || buttons > 31)
        return QStringLiteral("鼠标按钮选择无效，请重新选择。");
    if (merged.value("enabled").toBool() && !merged.value("keyboard_enabled").toBool() &&
        (!merged.value("mouse_enabled").toBool() || buttons == 0))
        return QStringLiteral("请至少启用键盘或一个鼠标按钮，或关闭 ESP32 主开关。");
    QSet<int> shortcuts;
    for (const auto* field : {"shortcut_pause", "shortcut_reconnect", "shortcut_release"}) {
        bool ok = false;
        const int key = merged.value(field).toInt(&ok);
        if (!ok || merged.value(field).toDouble() != key || key < 0 || key > 12 || (key && shortcuts.contains(key)))
            return QStringLiteral("快捷键请选择不重复的 F1–F12，或关闭该快捷键。");
        if (key) shortcuts.insert(key);
    }
    QStringList excluded;
    const auto tokens = merged.value("excluded_keys").toString().split(QRegularExpression(QStringLiteral("[,;，；\\s]+")), Qt::SkipEmptyParts);
    if (tokens.size() > 128) return QStringLiteral("排除按键过多，请减少到 128 项以内。");
    for (const auto& token : tokens) {
        const auto code = canonicalKeyCode(token);
        if (code.isEmpty()) return QStringLiteral("排除名单含无法识别的按键。请使用 A、F1、MetaLeft、ControlLeft 等名称。");
        if (!excluded.contains(code)) excluded.append(code);
    }
    merged["excluded_keys"] = excluded.join(QStringLiteral(", "));
    QSettings settings;
    settings.beginGroup(QStringLiteral("ESP32Hosts/") + hostUuid);
    // Write only known fields; an offline host retains its configuration.
    for (const auto* field : Fields) settings.setValue(field, merged[field]);
    settings.sync();
    return settings.status() == QSettings::NoError ? QString() : QStringLiteral("无法保存设置，请检查配置目录的写入权限。");
}

QString Esp32InputSettings::canonicalKeyCode(const QString& token)
{
    auto name = token.trimmed();
    if (name.size() == 1 && name.at(0).isLetter()) name = QStringLiteral("Key") + name.toUpper();
    else if (name.size() == 1 && name.at(0).isDigit()) name.prepend(QStringLiteral("Digit"));
    for (unsigned usage = 0; usage < 232; ++usage) {
        const auto* code = esp32KeyboardCode(usage);
        if (code && name.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) return QLatin1String(code);
    }
    if (name.compare(QStringLiteral("ContextMenu"), Qt::CaseInsensitive) == 0) return QStringLiteral("ContextMenu");
    for (unsigned i = 13; i <= 24; ++i) {
        const auto function = QStringLiteral("F%1").arg(i);
        if (name.compare(function, Qt::CaseInsensitive) == 0) return function;
    }
    return {};
}

void Esp32InputSettings::remove(const QString& hostUuid)
{
    if (!validUuid(hostUuid)) return;
    QSettings settings;
    settings.remove(QStringLiteral("ESP32Hosts/") + hostUuid);
    settings.sync();
}
