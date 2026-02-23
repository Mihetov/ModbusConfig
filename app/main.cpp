#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include "layers/application/application_layer.h"
#include "layers/transport/transport_layer.h"
#include <boost/json.hpp>

namespace json = boost::json;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "Starting ModbusConfig test application...";

    // 1. Создаем TransportManager для управления соединениями
    TransportManager transportManager;

    // 2. Создаем ApplicationCore для обработки бизнес-логики
    ApplicationCore applicationCore(&transportManager);

    // 3. Подключаем обработчик JSON-ответов
    QObject::connect(&applicationCore, &ApplicationCore::jsonResponseReady,
                     [](const json::value &response) {
                         qDebug() << "JSON Response received:";
                         qDebug() << QString::fromStdString(json::serialize(response));
                     });

    // 4. Запускаем TCP-сервер на порту 502 (стандартный Modbus TCP порт)
    bool serverStarted = transportManager.startServer(8001);
    if (!serverStarted) {
        qCritical() << "Failed to start TCP server on port 502";
        return -1;
    }

    qDebug() << "TCP server started on port 502. Waiting for connections...";

    // 5. Запускаем тест через некоторое время, чтобы сервер успел запуститься
    QTimer::singleShot(1000, [&]() {
        qDebug() << "Checking for active sessions...";

        if (transportManager.hasActiveSessions()) {
            qDebug() << "Active sessions found. Sending test command...";
        } else {
            qDebug() << "No active sessions yet. Creating test session...";

            // В реальном приложении здесь было бы подключение к Modbus-устройству
            // Для теста создадим фиктивное соединение через TCP-клиент
            // Но так как у нас сервер, то нужно подождать подключения клиента

            qDebug() << "Waiting for client connection... (Try connecting to localhost:502)";
            qDebug() << "For testing, you can use a Modbus client tool to connect to this server";
            qDebug() << "and send requests. The server will process them automatically.";
        }

        // 6. Через еще некоторое время отправим тестовый запрос (если есть активные сессии)
        QTimer::singleShot(5000, [&]() {
            if (transportManager.hasActiveSessions()) {
                qDebug() << "Sending test Modbus command...";

                // Создаем тестовую Modbus-команду (чтение holding регистров)
                MBCommand cmd;
                cmd.slaveId = 1;  // Modbus ID устройства
                cmd.functionCode = MB::utils::ReadAnalogOutputHoldingRegisters;
                cmd.startAddress = 0;  // Начальный адрес
                cmd.count = 10;        // Количество регистров

                auto session = transportManager.getFirstSession();
                applicationCore.sendModbusCommand(cmd, session);
            } else {
                qDebug() << "No active sessions after 5 seconds. Cannot send test command.";
                qDebug() << "Please connect a Modbus client to test the system.";
            }
        });
    });

    // 7. Добавляем обработчик для выхода через некоторое время (для теста)
    QTimer::singleShot(30000, [&]() {
        qDebug() << "Test completed after 30 seconds. Stopping...";
        transportManager.stop();
        QTimer::singleShot(1000, &app, &QCoreApplication::quit);
    });

    qDebug() << "Starting event loop...";
    return app.exec();
}
