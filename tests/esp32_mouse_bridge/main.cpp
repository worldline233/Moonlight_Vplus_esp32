#include "../../app/streaming/input/esp32mousebridge.h"
#include "../../app/settings/esp32inputsettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSettings>
#include <QtTest>
#include <QWebSocket>
#include <QWebSocketServer>

class BridgeTest : public QObject
{
    Q_OBJECT

    QTemporaryDir settingsDirectory;

    QString config(QTemporaryDir& dir, quint16 port, bool keyboard = false)
    {
        const auto path = dir.filePath(QStringLiteral("esp32-mouse.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        const QJsonObject object{
            {QStringLiteral("enabled"), true},
            {QStringLiteral("target_host"), QStringLiteral("test-host")},
            {QStringLiteral("address"), QStringLiteral("http://localhost:%1").arg(port)},
            {QStringLiteral("access_key"), QStringLiteral("123456")},
            {QStringLiteral("keyboard_enabled"), keyboard},
            {QStringLiteral("buttons"), QJsonArray{"left", "right", "middle", "back", "forward"}},
        };
        file.write(QJsonDocument(object).toJson());
        file.close();
        return path;
    }

    static void accept(QWebSocketServer& server, QList<QJsonObject>& messages,
                       QWebSocket*& peer, bool acknowledge = true, QJsonObject capabilities = {{"protocol", 2}, {"keyboard_nkro", true}, {"keyboard_max_keys", 16}})
    {
        QObject::connect(&server, &QWebSocketServer::newConnection, &server,
                         [&server, &messages, &peer, acknowledge, capabilities] {
            peer = server.nextPendingConnection();
            peer->setParent(&server);
            auto* socket = peer;
            QObject::connect(socket, &QWebSocket::textMessageReceived, &server,
                             [socket, &messages, acknowledge, capabilities](const QString& text) {
                const auto object = QJsonDocument::fromJson(text.toUtf8()).object();
                messages.append(object);
                if (acknowledge && object.value("type") == QStringLiteral("hello")) {
                    auto reply = capabilities;
                    reply.insert("type", QStringLiteral("macro_status"));
                    reply.insert("running", false);
                    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
                }
            });
        });
    }

    static int releaseCount(const QList<QJsonObject>& messages)
    {
        int count = 0;
        for (const auto& message : messages) {
            if (message.value("type") == QStringLiteral("release_all")) ++count;
        }
        return count;
    }

    static QList<QJsonArray> mouseStates(const QList<QJsonObject>& messages)
    {
        QList<QJsonArray> result;
        for (const auto& message : messages) {
            if (message.value("type") == QStringLiteral("mouse_state")) {
                result.append(message.value("buttons").toArray());
            }
        }
        return result;
    }

    static QList<QJsonObject> keyboardStates(const QList<QJsonObject>& messages)
    {
        QList<QJsonObject> result;
        for (const auto& message : messages) if (message.value("type") == QStringLiteral("state")) result.append(message);
        return result;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("ESP32IntegrationTests"));
        QCoreApplication::setApplicationName(QStringLiteral("HostProfiles"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }

    void hostProfilesAndValidation()
    {
        auto values = Esp32InputSettings::load("uuid-one");
        QVERIFY(!values.value("enabled").toBool());
        values["enabled"] = true;
        values["keyboard_enabled"] = true;
        values["address"] = "lan.worldline233.cloud";
        values["port"] = 38080;
        values["access_key"] = "123456";
        QVERIFY(Esp32InputSettings::save("uuid-one", values).isEmpty());
        const auto loaded = Esp32InputSettings::load("uuid-one");
        QCOMPARE(loaded, values);
        const auto url = Esp32InputSettings::endpoint(loaded);
        QCOMPARE(url.host(), QStringLiteral("lan.worldline233.cloud"));
        QCOMPARE(url.port(), 38080);
        QCOMPARE(url.path(), QStringLiteral("/ws"));
        QVERIFY(!Esp32InputSettings::load("uuid-two").value("enabled").toBool());
        values["address"] = "http://user:password@host/";
        QVERIFY(!Esp32InputSettings::save("uuid-one", values).isEmpty());
        QCOMPARE(Esp32InputSettings::load("uuid-one"), loaded);
        for (const auto& invalid : {"ws://host/?key=secret", "ws://host/other", "ws://host/#fragment", "ws://host:80"}) {
            values = loaded;
            values["address"] = invalid;
            QVERIFY(!Esp32InputSettings::save("uuid-one", values).isEmpty());
        }
        values = loaded;
        values["port"] = 80.5;
        QVERIFY(!Esp32InputSettings::save("uuid-one", values).isEmpty());
        QVERIFY(!Esp32InputSettings::save("", loaded).isEmpty());
        QVERIFY(!Esp32InputSettings::save("bad/uuid", loaded).isEmpty());
        Esp32InputSettings::remove("uuid-one");
        QVERIFY(!Esp32InputSettings::load("uuid-one").value("enabled").toBool());
    }

    void keyboardExclusiveFullStatesAndLocalRelease()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        QVERIFY(bridge.keyboardExclusive());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QVERIFY(bridge.handle(0, true));
        QTRY_COMPARE(mouseStates(messages).size(), 1);
        QVERIFY(bridge.handleKeyboard(224, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 1);
        QVERIFY(bridge.handleKeyboard(4, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 2);
        QCOMPARE(keyboardStates(messages).last().value("modifiers").toArray(), QJsonArray{"ControlLeft"});
        QCOMPARE(keyboardStates(messages).last().value("keys").toArray(), QJsonArray{"KeyA"});
        bridge.releaseKeyboard(); // The local exit/uncapture shortcut releases modifiers.
        QTRY_COMPARE(keyboardStates(messages).size(), 3);
        QVERIFY(keyboardStates(messages).last().value("modifiers").toArray().isEmpty());
        QVERIFY(keyboardStates(messages).last().value("keys").toArray().isEmpty());
        QVERIFY(bridge.handleKeyboard(4, false));
        QVERIFY(bridge.handleKeyboard(224, false));
        QVERIFY(bridge.handleKeyboard(104, true)); // Unsupported F13 still blocks native.
        QTest::qWait(30);
        QCOMPARE(keyboardStates(messages).size(), 3);
        // Keyboard-only release must not drop a mouse drag.
        QCOMPARE(mouseStates(messages).size(), 1);
        QVERIFY(bridge.handle(0, false));
        QTRY_COMPARE(mouseStates(messages).size(), 2);
        QVERIFY(mouseStates(messages).last().isEmpty());
        QVERIFY(bridge.handleKeyboard(5, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 4);
        const int releasesBefore = releaseCount(messages);
        bridge.releaseAll();
        // Allow a later heartbeat to arrive before checking the release.
        // The startup handshake also releases, so require one new message.
        QTest::qWait(400);
        QTRY_COMPARE(releaseCount(messages), releasesBefore + 1);
        QVERIFY(bridge.handleKeyboard(5, false));
        QTest::qWait(30);
        QCOMPARE(keyboardStates(messages).size(), 4);
    }

    void nkroTwelveKeysAndModifiers()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QCOMPARE(messages.first().value("protocol").toInt(), 2);
        for (unsigned usage = 4; usage < 16; ++usage) QVERIFY(bridge.handleKeyboard(usage, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 12);
        QCOMPARE(keyboardStates(messages).last().value("keys").toArray().size(), 12);
        QVERIFY(bridge.handleKeyboard(225, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 13);
        QCOMPARE(keyboardStates(messages).last().value("modifiers").toArray(), QJsonArray{"ShiftLeft"});
        QVERIFY(bridge.handleKeyboard(4, false));
        QTRY_COMPARE(keyboardStates(messages).size(), 14);
        QCOMPARE(keyboardStates(messages).last().value("keys").toArray().size(), 11);
        bridge.releaseKeyboard();
        QTRY_VERIFY(keyboardStates(messages).last().value("keys").toArray().isEmpty());
        for (unsigned usage = 4; usage < 16; ++usage) bridge.handleKeyboard(usage, false);
        QVERIFY(bridge.handleKeyboard(225, false));
        QVERIFY(bridge.handleKeyboard(4, true));
        QTRY_COMPARE(keyboardStates(messages).last().value("keys").toArray(), QJsonArray{"KeyA"});
    }

    void incompatibleFirmware_data()
    {
        QTest::addColumn<QJsonObject>("capabilities");
        QTest::newRow("legacy") << QJsonObject{};
        QTest::newRow("protocol-1") << QJsonObject{{"protocol", 1}, {"keyboard_nkro", true}, {"keyboard_max_keys", 16}};
        QTest::newRow("missing-nkro") << QJsonObject{{"protocol", 2}, {"keyboard_max_keys", 16}};
        QTest::newRow("six-key") << QJsonObject{{"protocol", 2}, {"keyboard_nkro", true}, {"keyboard_max_keys", 6}};
        QTest::newRow("fractional") << QJsonObject{{"protocol", 2}, {"keyboard_nkro", true}, {"keyboard_max_keys", 16.5}};
        QTest::newRow("wrong-type") << QJsonObject{{"protocol", 2}, {"keyboard_nkro", true}, {"keyboard_max_keys", "16"}};
    }

    void incompatibleFirmware()
    {
        QFETCH(QJsonObject, capabilities);
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer, true, capabilities);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        // Observe the socket closure after rejecting the incompatible hello.
        QTRY_VERIFY(peer != nullptr);
        QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
        QVERIFY(!bridge.ready());
        QVERIFY(bridge.handleKeyboard(4, true));
        QVERIFY(bridge.handleKeyboard(4, false));
        QVERIFY(keyboardStates(messages).isEmpty());
    }

    void keyboardDisconnectBlocksNativeAndHeldReplay()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QVERIFY(bridge.handleKeyboard(4, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 1);
        peer->abort();
        QTRY_VERIFY(!bridge.ready());
        QVERIFY(bridge.handleKeyboard(5, true));
        QVERIFY(bridge.handleKeyboard(4, false));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 5000);
        QVERIFY(bridge.handleKeyboard(5, false));
        QTest::qWait(30);
        QCOMPARE(keyboardStates(messages).size(), 1);
        QVERIFY(bridge.handleKeyboard(6, true));
        QVERIFY(bridge.handleKeyboard(6, false));
        QTRY_COMPARE(keyboardStates(messages).size(), 3);
        QCOMPARE(keyboardStates(messages)[1].value("keys").toArray(), QJsonArray{"KeyC"});
        QVERIFY(keyboardStates(messages).last().value("keys").toArray().isEmpty());
    }

    void hostUuidLoadsKeyboardOnlyProfile()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        auto values = Esp32InputSettings::load("keyboard-only-uuid");
        values["enabled"] = true;
        values["keyboard_enabled"] = true;
        values["mouse_enabled"] = false;
        values["address"] = "localhost";
        values["port"] = server.serverPort();
        values["access_key"] = "123456";
        QVERIFY(Esp32InputSettings::save("keyboard-only-uuid", values).isEmpty());
        {
            Esp32MouseBridge wrong("same-name", "other-uuid");
            QVERIFY(!wrong.configured());
            QVERIFY(!wrong.keyboardExclusive());
            QVERIFY(!wrong.handleKeyboard(4, true));
            Esp32MouseBridge bridge("renamed-host", "keyboard-only-uuid");
            QVERIFY(bridge.keyboardExclusive());
            QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
            QVERIFY(!bridge.handle(0, true));
            QVERIFY(!bridge.handle(0, false));
            QVERIFY(bridge.handleKeyboard(4, true));
            QVERIFY(bridge.handleKeyboard(4, false));
            QTRY_COMPARE(keyboardStates(messages).size(), 2);
            QVERIFY(mouseStates(messages).isEmpty());
        }
        Esp32InputSettings::remove("keyboard-only-uuid");
    }

    void exclusiveKeyboardStaysBlockedWithInvalidConfiguration()
    {
        // Simulate a manually damaged stored profile: no valid worker exists,
        // but exclusive mode must not silently enable native keyboard sends.
        QSettings settings;
        settings.setValue("ESP32Hosts/broken-uuid/enabled", true);
        settings.setValue("ESP32Hosts/broken-uuid/keyboard_enabled", true);
        settings.sync();
        Esp32MouseBridge bridge("host", "broken-uuid");
        QVERIFY(bridge.keyboardExclusive());
        QVERIFY(!bridge.ready());
        QVERIFY(!bridge.configured());
        QVERIFY(bridge.handleKeyboard(4, true));
        QVERIFY(bridge.handleKeyboard(4, false));
        Esp32InputSettings::remove("broken-uuid");
    }

    void shortcutsAndExclusionValidation()
    {
        auto values = Esp32InputSettings::load("custom-uuid");
        values["excluded_keys"] = "a, MetaLeft, f12, A";
        values["shortcut_pause"] = 1;
        values["shortcut_reconnect"] = 2;
        values["shortcut_release"] = 0;
        QVERIFY(Esp32InputSettings::save("custom-uuid", values).isEmpty());
        auto stored = Esp32InputSettings::load("custom-uuid");
        QCOMPARE(stored.value("excluded_keys").toString(), QStringLiteral("KeyA, MetaLeft, F12"));
        QCOMPARE(stored.value("shortcut_pause").toInt(), 1);
        QCOMPARE(stored.value("shortcut_release").toInt(), 0);
        values["shortcut_reconnect"] = 1;
        QVERIFY(!Esp32InputSettings::save("custom-uuid", values).isEmpty());
        QCOMPARE(Esp32InputSettings::load("custom-uuid"), stored);
        values["shortcut_reconnect"] = 2;
        values["excluded_keys"] = "NotAKey";
        QVERIFY(!Esp32InputSettings::save("custom-uuid", values).isEmpty());
        values["excluded_keys"] = "";
        values["shortcut_pause"] = 13;
        QVERIFY(!Esp32InputSettings::save("custom-uuid", values).isEmpty());
        values["shortcut_pause"] = 1.5;
        QVERIFY(!Esp32InputSettings::save("custom-uuid", values).isEmpty());
        QCOMPARE(Esp32InputSettings::canonicalKeyCode("0"), QStringLiteral("Digit0"));
        QCOMPARE(Esp32InputSettings::canonicalKeyCode("F24"), QStringLiteral("F24"));
        Esp32InputSettings::remove("custom-uuid");
    }

    void excludedPhysicalKeysNeverReachUsb()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        auto values = Esp32InputSettings::load("excluded-uuid");
        values["enabled"] = true;
        values["keyboard_enabled"] = true;
        values["address"] = "localhost";
        values["port"] = server.serverPort();
        values["access_key"] = "123456";
        values["excluded_keys"] = "A, MetaLeft";
        values["mouse_buttons"] = 1;
        QVERIFY(Esp32InputSettings::save("excluded-uuid", values).isEmpty());
        {
            Esp32MouseBridge bridge("host", "excluded-uuid");
            QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
            QVERIFY(!bridge.handle(1, true));
            QVERIFY(!bridge.handle(1, false));
            QVERIFY(bridge.handle(0, true));
            QVERIFY(bridge.handle(0, false));
            QTRY_COMPARE(mouseStates(messages).size(), 2);
            QVERIFY(bridge.keyboardExcluded(4));
            QVERIFY(bridge.handleKeyboard(4, true));
            QVERIFY(bridge.handleKeyboard(4, false));
            QVERIFY(bridge.handleKeyboard(227, true));
            QVERIFY(bridge.handleKeyboard(227, false));
            QTest::qWait(30);
            QVERIFY(keyboardStates(messages).isEmpty());
            // Alt/Win swap must test exclusions against the physical key.
            QVERIFY(bridge.handleKeyboard(227, true, 226));
            QTRY_COMPARE(keyboardStates(messages).size(), 1);
            QCOMPARE(keyboardStates(messages).last().value("modifiers").toArray(), QJsonArray{"MetaLeft"});
            QVERIFY(bridge.handleKeyboard(227, false, 226));
            QTRY_COMPARE(keyboardStates(messages).size(), 2);
            QVERIFY(bridge.handleKeyboard(5, true));
            QVERIFY(bridge.handleKeyboard(5, false));
            QTRY_COMPARE(keyboardStates(messages).size(), 4);
        }
        Esp32InputSettings::remove("excluded-uuid");
    }

    void pauseBlocksUsbWithoutNativeFallbackOrHeldReplay()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QVERIFY(bridge.handle(0, true));
        QVERIFY(bridge.handleKeyboard(4, true));
        QTRY_COMPARE(mouseStates(messages).size(), 1);
        QTRY_COMPARE(keyboardStates(messages).size(), 1);
        const int releasesBefore = releaseCount(messages);
        QCOMPARE(bridge.setPaused(true), std::uint8_t(0));
        QTRY_COMPARE(releaseCount(messages), releasesBefore + 1);
        QVERIFY(bridge.status().paused);
        QVERIFY(bridge.ready());
        QVERIFY(bridge.handle(1, true));
        QVERIFY(bridge.handleKeyboard(5, true));
        QTest::qWait(30);
        QCOMPARE(mouseStates(messages).size(), 1);
        QCOMPARE(keyboardStates(messages).size(), 1);
        bridge.setPaused(false);
        QVERIFY(bridge.handle(1, false));
        QVERIFY(bridge.handle(0, false));
        QVERIFY(bridge.handleKeyboard(4, false));
        QVERIFY(bridge.handleKeyboard(5, false));
        QTest::qWait(30);
        QCOMPARE(mouseStates(messages).size(), 1);
        QCOMPARE(keyboardStates(messages).size(), 1);
        QVERIFY(bridge.handle(2, true));
        QVERIFY(bridge.handle(2, false));
        QVERIFY(bridge.handleKeyboard(6, true));
        QVERIFY(bridge.handleKeyboard(6, false));
        QTRY_COMPARE(mouseStates(messages).size(), 3);
        QTRY_COMPARE(keyboardStates(messages).size(), 3);
    }

    void manualReconnectAndControlRtt()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.status().rttMs >= 0, 3000);
        QCOMPARE(bridge.status().reconnects, 0u);
        QVERIFY(bridge.handleKeyboard(4, true));
        QTRY_COMPARE(keyboardStates(messages).size(), 1);
        bridge.reconnect();
        QVERIFY(!bridge.ready());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QCOMPARE(bridge.status().reconnects, 1u);
        QVERIFY(bridge.handleKeyboard(4, false));
        QTest::qWait(30);
        QCOMPARE(keyboardStates(messages).size(), 1);
        QVERIFY(bridge.handleKeyboard(5, true));
        QVERIFY(bridge.handleKeyboard(5, false));
        QTRY_COMPARE(keyboardStates(messages).size(), 3);
    }

    void decoderStatusReaderOutlivesInputFacade()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32StatusReader reader;
        {
            Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort(), true));
            reader = bridge.statusReader();
            QVERIFY(reader);
            QTRY_VERIFY_WITH_TIMEOUT(reader().ready, 4000);
            QVERIFY(reader().enabled);
            QVERIFY(reader().keyboardUsb);
            QCOMPARE(reader().mouseButtons, std::uint8_t(31));
            bridge.setPaused(true);
            QVERIFY(reader().paused);
            bridge.setPaused(false);
            QVERIFY(!reader().paused);
        }
        // The decoder may retain a snapshot provider while input is torn down.
        // Its shared state remains valid and reports the closed connection.
        QVERIFY(!reader().ready);
        QCOMPARE(reader().rttMs, -1);
        QVERIFY(reader().keyboardUsb);
    }

    void selectedClicksAndSequence()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        for (unsigned i = 0; i < 100; ++i) {
            QVERIFY(bridge.handle(i % 5, true));
            QVERIFY(bridge.handle(i % 5, false));
        }
        QTRY_COMPARE_WITH_TIMEOUT(mouseStates(messages).size(), 200, 4000);
        int previous = 0;
        for (const auto& message : messages) {
            const int sequence = message.value("seq").toInt();
            QVERIFY(sequence > previous);
            previous = sequence;
        }
        QCOMPARE(mouseStates(messages).last().size(), 0);
        QVERIFY(peer != nullptr);
    }

    void focusReleaseAndNoHeldReplay()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QVERIFY(bridge.handle(0, true));
        QTRY_COMPARE(mouseStates(messages).size(), 1);
        const int releasesBefore = releaseCount(messages);
        QCOMPARE(bridge.releaseAll(), std::uint8_t(0));
        QTRY_COMPARE(releaseCount(messages), releasesBefore + 1);
        QVERIFY(bridge.handle(0, false));
        QTest::qWait(30);
        QCOMPARE(mouseStates(messages).size(), 1);
        QVERIFY(bridge.handle(0, true));
        QVERIFY(bridge.handle(0, false));
        QTRY_COMPARE(mouseStates(messages).size(), 3);
    }

    void disconnectDoesNotSendOldReleaseToNative()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 4000);
        QVERIFY(bridge.handle(0, true));
        QTRY_COMPARE(mouseStates(messages).size(), 1);
        peer->abort();
        QTRY_VERIFY(!bridge.ready());
        QVERIFY(bridge.handle(0, false));
        QVERIFY(!bridge.handle(1, true));
        QVERIFY(!bridge.handle(1, false));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ready(), 5000);
        QVERIFY(bridge.handle(0, true));
        QVERIFY(bridge.handle(0, false));
        QTRY_COMPARE(mouseStates(messages).size(), 3);
    }

    void upgradeAloneIsNotReady()
    {
        QWebSocketServer server(QStringLiteral("test"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QJsonObject> messages;
        QWebSocket* peer = nullptr;
        accept(server, messages, peer, false);
        QTemporaryDir dir;
        Esp32MouseBridge bridge("test-host", "uuid", config(dir, server.serverPort()));
        QTRY_VERIFY(!messages.isEmpty());
        QVERIFY(!bridge.ready());
        QVERIFY(!bridge.handle(0, true));
        QVERIFY(!bridge.handle(0, false));
        peer->sendTextMessage(QStringLiteral("{\"type\":\"error\",\"message\":\"test rejection\"}"));
        QTest::qWait(50);
        QVERIFY(!bridge.ready());
    }

    void wrongStreamedHostDoesNotConnect()
    {
        QTemporaryDir dir;
        Esp32MouseBridge bridge("another-host", "uuid", config(dir, 38080));
        QVERIFY(!bridge.ready());
        QVERIFY(!bridge.handle(0, true));
        QVERIFY(!bridge.handle(0, false));
    }
};

QTEST_GUILESS_MAIN(BridgeTest)
#include "main.moc"
