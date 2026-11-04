#include "esp32mousebridge.h"
#include "esp32mouserouter.h"
#include "esp32keyboardrouter.h"
#include "../../settings/esp32inputsettings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QSet>

#include <atomic>
#include <limits>

namespace {
constexpr unsigned MaxPendingReports = 256;
constexpr qint64 MaxPendingBytes = 64 * 1024;
const char* const ButtonNames[] = {"left", "right", "middle", "back", "forward"};

struct State {
    std::atomic<std::uint64_t> epoch{0};
    std::atomic<std::uint64_t> inputGeneration{0};
    std::atomic<std::uint64_t> keyboardGeneration{0};
    std::atomic<unsigned> pending{0};
    std::atomic<int> rttMs{-1};
    std::atomic<unsigned> reconnects{0};
    std::atomic<bool> paused{false};
};

struct Config {
    QUrl url;
    std::uint8_t buttons = 0;
    bool valid = false;
    bool keyboard = false;
    bool enabled = false;
    QSet<unsigned> excluded;
};

Config loadConfig(const QString& hostName, const QString& hostUuid, const QString& configPath)
{
    Config config;
    if (configPath.isEmpty()) {
        // Normal sessions use the same QSettings store as Moonlight, keyed by
        // host UUID. A test-only path keeps protocol tests independent of it.
        const auto values = Esp32InputSettings::load(hostUuid);
        if (!values.value("enabled").toBool()) return config;
        config.enabled = true;
        config.keyboard = values.value("keyboard_enabled").toBool();
        const auto excluded = values.value("excluded_keys").toString().split(',', Qt::SkipEmptyParts);
        for (const auto& token : excluded) {
            const auto code = Esp32InputSettings::canonicalKeyCode(token);
            if (code.isEmpty()) continue;
            for (unsigned usage = 0; usage < 232; ++usage) {
                const auto* name = esp32KeyboardCode(usage);
                if (name && code == QLatin1String(name)) config.excluded.insert(usage);
            }
            for (unsigned i = 13; i <= 24; ++i) {
                if (code == QStringLiteral("F%1").arg(i)) config.excluded.insert(104 + i - 13);
            }
            if (code == QStringLiteral("ContextMenu")) config.excluded.insert(101);
        }
        const int buttons = values.value("mouse_buttons").toInt();
        config.buttons = values.value("mouse_enabled").toBool() && buttons >= 0 && buttons <= 31 ? buttons : 0;
        config.url = Esp32InputSettings::endpoint(values);
        config.valid = !config.url.isEmpty() && (config.buttons != 0 || config.keyboard);
        if (!config.valid) qWarning("ESP32 input: invalid host configuration; exclusive keyboard stays blocked");
        return config;
    }
    QFile file(configPath);
    if (!file.exists()) return config;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) {
        qWarning("ESP32 mouse: cannot read configuration");
        return config;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("ESP32 mouse: invalid configuration JSON");
        return config;
    }
    const auto object = doc.object();
    if (!object.value(QStringLiteral("enabled")).toBool(false)) return config;
    const auto uuid = object.value(QStringLiteral("target_uuid")).toString();
    const auto name = object.value(QStringLiteral("target_host")).toString();
    // A UUID takes priority. Never silently send input to another streamed host.
    if ((!uuid.isEmpty() && uuid != hostUuid) ||
        (uuid.isEmpty() && (name.isEmpty() || name.compare(hostName, Qt::CaseInsensitive) != 0))) {
        qInfo("ESP32 mouse: configuration does not match this streamed host");
        return config;
    }
    config.enabled = true;
    config.keyboard = object.value(QStringLiteral("keyboard_enabled")).toBool(false);
    auto address = object.value(QStringLiteral("address")).toString().trimmed();
    if (!address.contains(QStringLiteral("://"))) address.prepend(QStringLiteral("ws://"));
    QUrl url(address, QUrl::StrictMode);
    if (url.scheme() == QStringLiteral("http")) url.setScheme(QStringLiteral("ws"));
    if (url.scheme() == QStringLiteral("https")) url.setScheme(QStringLiteral("wss"));
    const auto path = url.path();
    const auto portValue = object.value(QStringLiteral("port"));
    const int separatePort = portValue.toInt(-1);
    const auto key = object.value(QStringLiteral("access_key")).toString();
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QStringLiteral("ws") && url.scheme() != QStringLiteral("wss")) ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (path != QString() && path != QStringLiteral("/") && path != QStringLiteral("/ws")) ||
        url.port() == 0 || key.size() != 6 ||
        (!portValue.isUndefined() && (!portValue.isDouble() ||
            portValue.toDouble() != separatePort || separatePort < 1)) ||
        (separatePort != -1 && (separatePort < 1 || separatePort > 65535 ||
            (url.port() != -1 && separatePort != url.port())))) {
        qWarning("ESP32 mouse: invalid address, key, or conflicting port");
        return config;
    }
    if (separatePort != -1) url.setPort(separatePort);
    url.setPath(QStringLiteral("/ws"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    url.setQuery(query);
    for (const auto& button : object.value(QStringLiteral("buttons")).toArray()) {
        bool found = false;
        for (unsigned i = 0; i < 5; ++i) {
            if (button.isString() && button.toString() == QLatin1String(ButtonNames[i])) {
                config.buttons |= 1u << i;
                found = true;
            }
        }
        if (!found) {
            qWarning("ESP32 mouse: unknown button in configuration");
            return Config{};
        }
    }
    if (config.buttons == 0 && !config.keyboard) {
        qWarning("ESP32 mouse: no mouse buttons selected");
        return config;
    }
    config.url = url;
    config.valid = true;
    return config;
}

class Worker : public QObject
{
public:
    Worker(Config config, std::shared_ptr<State> state, std::function<void(const QString&)> notify)
        : m_Config(std::move(config)), m_State(std::move(state)), m_Notify(std::move(notify)) {}

    void start()
    {
        status(QStringLiteral("ESP32 正在连接"));
        m_Socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        m_Socket->setMaxAllowedIncomingFrameSize(4096);
        m_Socket->setMaxAllowedIncomingMessageSize(4096);
        m_Reconnect = new QTimer(this);
        m_Reconnect->setSingleShot(true);
        m_Deadline = new QTimer(this);
        m_Deadline->setSingleShot(true);
        m_Ping = new QTimer(this);
        m_Ping->setInterval(200);
        connect(m_Reconnect, &QTimer::timeout, this, [this] { open(); });
        connect(m_Deadline, &QTimer::timeout, this, [this] { fail(); });
        connect(m_Ping, &QTimer::timeout, this, [this] {
            if (m_State->epoch.load() != 0) {
                if (m_LastPong.elapsed() > 3000) { fail(); return; }
                if (send(QStringLiteral("ping")) && ++m_PingTicks % 5 == 0) {
                    m_PingToken = QByteArray::number(++m_PingSerial);
                    m_Socket->ping(m_PingToken);
                }
            }
        });
        connect(m_Socket, &QWebSocket::pong, this, [this](quint64 elapsed, const QByteArray& payload) {
            if (m_State->epoch.load() == 0 || payload != m_PingToken || m_PingToken.isEmpty()) return;
            m_LastPong.start();
            m_State->rttMs.store(static_cast<int>(qMin<quint64>(elapsed, 60000)));
        });
        connect(m_Socket, &QWebSocket::connected, this, [this] {
            m_Sequence = 0;
            m_AwaitingHello = true;
            QJsonObject fields;
            fields.insert(QStringLiteral("protocol"), 2);
            fields.insert(QStringLiteral("client"), QStringLiteral("moonlight-vplus-esp32-nkro"));
            send(QStringLiteral("hello"), fields);
        });
        connect(m_Socket, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                fail(); return;
            }
            const auto message = doc.object();
            const auto type = message.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("error")) {
                qWarning("ESP32 mouse: protocol rejected; check control key and other controllers");
                fail(); return;
            }
            if (m_AwaitingHello && type == QStringLiteral("macro_status") &&
                message.value(QStringLiteral("running")).isBool() &&
                !message.value(QStringLiteral("running")).toBool()) {
                if (message.value(QStringLiteral("protocol")).toDouble() != 2 ||
                    !message.value(QStringLiteral("keyboard_nkro")).toBool(false) ||
                    message.value(QStringLiteral("keyboard_max_keys")).toDouble() != Esp32KeyboardRouter::MaxKeys) {
                    fail(QStringLiteral("ESP32 固件不匹配：请烧录协议 2 NKRO 固件"));
                    return;
                }
                m_AwaitingHello = false;
                if (!send(QStringLiteral("release_all"))) return;
                m_Deadline->stop();
                m_LastPong.start();
                m_PingTicks = 0;
                m_State->epoch.store(++m_NextEpoch);
                m_State->reconnects.store(static_cast<unsigned>(m_NextEpoch - 1));
                m_Ping->start();
                status(m_State->paused.load() ? QStringLiteral("ESP32 已连接；USB 输入仍处于暂停状态") :
                       m_Config.keyboard ? QStringLiteral("ESP32 已连接：键盘通过 USB，Moonlight 键盘发送已屏蔽")
                                         : QStringLiteral("ESP32 已连接：USB 鼠标点击已启用"));
                qInfo("ESP32 input: USB route ready; protocol 2 NKRO");
            }
        });
        connect(m_Socket, &QWebSocket::disconnected, this, [this] { fail(); });
        connect(m_Socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            // Do not log the request URL/error text: the query contains the key.
            fail();
        });
        open();
    }

    void mouse(std::uint64_t epoch, std::uint8_t buttons)
    {
        if (epoch == 0 || epoch != m_State->epoch.load()) return;
        QJsonArray array;
        for (unsigned i = 0; i < 5; ++i) {
            if (buttons & (1u << i)) array.append(QLatin1String(ButtonNames[i]));
        }
        QJsonObject fields;
        fields.insert(QStringLiteral("buttons"), array);
        send(QStringLiteral("mouse_state"), fields);
    }

    void keyboard(std::uint64_t epoch, const Esp32KeyboardRouter::Report& report)
    {
        if (epoch == 0 || epoch != m_State->epoch.load()) return;
        QJsonArray modifiers, keys;
        for (unsigned i = 0; i < 8; ++i) {
            if (report.modifiers & (1u << i)) modifiers.append(QLatin1String(esp32KeyboardCode(224 + i)));
        }
        for (auto usage : report.keys) {
            if (usage) keys.append(QLatin1String(esp32KeyboardCode(usage)));
        }
        send(QStringLiteral("state"), {{QStringLiteral("modifiers"), modifiers}, {QStringLiteral("keys"), keys}});
    }

    void release()
    {
        if (m_State->epoch.load() != 0) send(QStringLiteral("release_all"));
    }

    void stop()
    {
        m_Stopping = true;
        m_Reconnect->stop(); m_Deadline->stop(); m_Ping->stop();
        release();
        m_State->epoch.store(0);
        m_Socket->flush();
        m_Socket->close();
    }

    void reconnect()
    {
        if (m_Stopping) return;
        m_Failing = true;
        if (m_Socket->state() == QAbstractSocket::ConnectedState && !m_AwaitingHello)
            send(QStringLiteral("release_all"));
        m_State->epoch.store(0);
        m_State->rttMs.store(-1);
        m_Ping->stop(); m_Deadline->stop(); m_Reconnect->stop();
        m_AwaitingHello = false;
        m_Socket->flush();
        m_Socket->abort();
        m_Failing = false;
        status(QStringLiteral("ESP32 正在重新连接"));
        open();
    }

    void fail(const QString& reason = QString())
    {
        const bool wasReady = m_State->epoch.exchange(0) != 0;
        m_State->rttMs.store(-1);
        if (m_Failing || m_Stopping) return;
        m_Failing = true;
        m_AwaitingHello = false;
        m_Ping->stop(); m_Deadline->stop();
        m_Socket->abort();
        if (wasReady) qWarning("ESP32 input: USB route lost; retrying");
        status(!reason.isEmpty() ? reason : m_Config.keyboard ? QStringLiteral("ESP32 未连接：键盘发送已暂停，正在重连；退出快捷键仍可使用")
                                 : QStringLiteral("ESP32 未连接：正在重连，鼠标点击暂用 Moonlight"));
        if (!m_Reconnect->isActive()) m_Reconnect->start(2000);
        m_Failing = false;
    }

private:
    void status(const QString& text)
    {
        if (text == m_LastStatus) return;
        m_LastStatus = text;
        if (m_Notify) m_Notify(text);
    }

    void open()
    {
        if (m_Stopping) return;
        m_State->epoch.store(0);
        m_State->rttMs.store(-1);
        m_PingToken.clear();
        m_Deadline->start(5000);
        qInfo("ESP32 input: connecting; exclusive keyboard never falls back to native");
        m_Socket->open(m_Config.url);
    }

    bool send(const QString& type, QJsonObject message = {})
    {
        if (m_Sequence == std::numeric_limits<std::uint32_t>::max() ||
            m_Socket->state() != QAbstractSocket::ConnectedState ||
            m_Socket->bytesToWrite() > MaxPendingBytes) {
            fail(); return false;
        }
        message.insert(QStringLiteral("type"), type);
        message.insert(QStringLiteral("seq"), static_cast<double>(++m_Sequence));
        const auto data = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
        if (m_Socket->sendTextMessage(data) <= 0) { fail(); return false; }
        return true;
    }

    Config m_Config;
    std::shared_ptr<State> m_State;
    QWebSocket* m_Socket = nullptr;
    QTimer* m_Reconnect = nullptr;
    QTimer* m_Deadline = nullptr;
    QTimer* m_Ping = nullptr;
    std::uint32_t m_Sequence = 0;
    std::uint64_t m_NextEpoch = 0;
    bool m_Failing = false;
    bool m_Stopping = false;
    bool m_AwaitingHello = false;
    QElapsedTimer m_LastPong;
    unsigned m_PingTicks = 0;
    unsigned m_PingSerial = 0;
    QByteArray m_PingToken;
    std::function<void(const QString&)> m_Notify;
    QString m_LastStatus;
};
} // namespace

struct Esp32MouseBridge::Impl {
    Config config;
    std::shared_ptr<State> state = std::make_shared<State>();
    Esp32MouseRouter router;
    Esp32KeyboardRouter keyboardRouter;
    QThread thread;
    Worker* worker = nullptr;
    std::function<void(const QString&)> notify;
};

Esp32MouseBridge::Esp32MouseBridge(const QString& hostName, const QString& hostUuid,
                                 const QString& configPath, std::function<void(const QString&)> notify)
    : m_Impl(new Impl)
{
    m_Impl->config = loadConfig(hostName, hostUuid, configPath);
    m_Impl->notify = notify;
    if (!m_Impl->config.valid) return;
    auto* worker = new Worker(m_Impl->config, m_Impl->state, std::move(notify));
    m_Impl->worker = worker;
    worker->moveToThread(&m_Impl->thread);
    QObject::connect(&m_Impl->thread, &QThread::started, worker, [worker] { worker->start(); });
    QObject::connect(&m_Impl->thread, &QThread::finished, worker, &QObject::deleteLater);
    m_Impl->thread.start();
}

Esp32MouseBridge::~Esp32MouseBridge()
{
    if (!m_Impl->worker) return;
    m_Impl->state->inputGeneration.fetch_add(1);
    auto* worker = m_Impl->worker;
    QMetaObject::invokeMethod(worker, [worker] { worker->stop(); }, Qt::BlockingQueuedConnection);
    m_Impl->thread.quit();
    m_Impl->thread.wait();
}

bool Esp32MouseBridge::handle(unsigned hidButton, bool down)
{
    if (!m_Impl->worker) return false;
    const bool paused = m_Impl->state->paused.load();
    const auto epoch = paused ? 0 : m_Impl->state->epoch.load();
    const auto decision = m_Impl->router.handle(hidButton, down, epoch, m_Impl->config.buttons, paused);
    if (decision.sendUsb) {
        auto* worker = m_Impl->worker;
        const auto state = m_Impl->state;
        const auto generation = state->inputGeneration.load();
        if (state->pending.fetch_add(1) >= MaxPendingReports) {
            state->pending.fetch_sub(1);
            // Invalidate this epoch immediately; pending frames cannot cross reconnects.
            if (state->epoch.exchange(0) != 0) {
                QMetaObject::invokeMethod(worker, [worker] { worker->fail(); }, Qt::QueuedConnection);
            }
        }
        else {
            QMetaObject::invokeMethod(worker, [worker, state, epoch, generation, buttons = decision.usbButtons] {
                state->pending.fetch_sub(1);
                if (generation == state->inputGeneration.load()) worker->mouse(epoch, buttons);
            }, Qt::QueuedConnection);
        }
    }
    return decision.consume;
}

bool Esp32MouseBridge::keyboardExclusive() const
{
    return m_Impl->config.keyboard;
}

bool Esp32MouseBridge::keyboardExcluded(unsigned scancode) const
{
    return keyboardExclusive() && m_Impl->config.excluded.contains(scancode);
}

bool Esp32MouseBridge::handleKeyboard(unsigned scancode, bool down, unsigned physicalScancode)
{
    if (!keyboardExclusive()) return false;
    if (keyboardExcluded(physicalScancode ? physicalScancode : scancode)) return true;
    if (!m_Impl->worker) return true;
    const auto epoch = m_Impl->state->paused.load() ? 0 : m_Impl->state->epoch.load();
    const auto report = m_Impl->keyboardRouter.handle(scancode, down, epoch);
    if (report.send) {
        auto* worker = m_Impl->worker;
        const auto state = m_Impl->state;
        const auto generation = state->inputGeneration.load();
        const auto keyboardGeneration = state->keyboardGeneration.load();
        if (state->pending.fetch_add(1) >= MaxPendingReports) {
            state->pending.fetch_sub(1);
            if (state->epoch.exchange(0) != 0)
                QMetaObject::invokeMethod(worker, [worker] { worker->fail(); }, Qt::QueuedConnection);
        }
        else {
            QMetaObject::invokeMethod(worker, [worker, state, epoch, generation, keyboardGeneration, report] {
                state->pending.fetch_sub(1);
                if (generation == state->inputGeneration.load() &&
                    keyboardGeneration == state->keyboardGeneration.load()) worker->keyboard(epoch, report);
            }, Qt::QueuedConnection);
        }
    }
    // Unsupported keys are also consumed: never leak into the native path.
    return true;
}

void Esp32MouseBridge::releaseKeyboard()
{
    if (!keyboardExclusive() || !m_Impl->worker) return;
    m_Impl->keyboardRouter.cancel();
    m_Impl->state->keyboardGeneration.fetch_add(1);
    const auto epoch = m_Impl->state->epoch.load();
    auto* worker = m_Impl->worker;
    QMetaObject::invokeMethod(worker, [worker, epoch] {
        worker->keyboard(epoch, Esp32KeyboardRouter::Report{});
    }, Qt::QueuedConnection);
}

bool Esp32MouseBridge::ready() const
{
    return m_Impl->state->epoch.load() != 0;
}

bool Esp32MouseBridge::configured() const
{
    return m_Impl->worker != nullptr;
}

Esp32InputStatus Esp32MouseBridge::status() const
{
    Esp32InputStatus result;
    result.enabled = m_Impl->config.enabled;
    result.configured = configured();
    result.ready = ready();
    result.paused = m_Impl->state->paused.load();
    result.rttMs = result.ready ? m_Impl->state->rttMs.load() : -1;
    result.reconnects = m_Impl->state->reconnects.load();
    result.keyboardUsb = keyboardExclusive();
    result.mouseButtons = m_Impl->config.buttons;
    result.hasExcludedKeys = !m_Impl->config.excluded.isEmpty();
    return result;
}

Esp32StatusReader Esp32MouseBridge::statusReader() const
{
    const auto state = m_Impl->state;
    const auto configuration = status();
    // No facade, worker or Session pointer is captured. All changing fields
    // are atomic, so decoding never reads the SDL-thread routing state.
    return [state, configuration] {
        auto snapshot = configuration;
        snapshot.ready = state->epoch.load() != 0;
        snapshot.paused = state->paused.load();
        snapshot.rttMs = snapshot.ready ? state->rttMs.load() : -1;
        snapshot.reconnects = state->reconnects.load();
        return snapshot;
    };
}

std::uint8_t Esp32MouseBridge::setPaused(bool paused)
{
    const auto native = releaseAll();
    m_Impl->state->paused.store(paused);
    if (m_Impl->notify) m_Impl->notify(paused ? QStringLiteral("ESP32 USB 输入已暂停；本地退出快捷键仍可使用")
                                            : ready() ? QStringLiteral("ESP32 USB 输入已恢复；请松开后重新按键")
                                                      : QStringLiteral("ESP32 已取消暂停，等待连接；请松开后重新按键"));
    return native;
}

std::uint8_t Esp32MouseBridge::reconnect()
{
    const auto native = releaseAll();
    if (m_Impl->worker) {
        // Invalidate ready immediately so input cannot race the worker's abort.
        m_Impl->state->epoch.store(0);
        auto* worker = m_Impl->worker;
        QMetaObject::invokeMethod(worker, [worker] { worker->reconnect(); }, Qt::QueuedConnection);
    }
    return native;
}

std::uint8_t Esp32MouseBridge::releaseAll()
{
    if (!m_Impl->worker) return 0;
    const auto native = m_Impl->router.cancel();
    m_Impl->keyboardRouter.cancel();
    m_Impl->state->inputGeneration.fetch_add(1);
    auto* worker = m_Impl->worker;
    QMetaObject::invokeMethod(worker, [worker] { worker->release(); }, Qt::QueuedConnection);
    return native;
}
