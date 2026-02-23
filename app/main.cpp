#include <QCoreApplication>
#include <QTextStream>

#include "core/api/ConsoleJsonApi.h"
#include "core/application/ModbusApplicationService.h"
#include "core/protocol/ModbusProtocolService.h"
#include "infrastructure/transport/InMemoryModbusTransport.h"

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    transport::InMemoryModbusTransport transport;
    protocol::ModbusProtocolService protocolService(transport);
    application::ModbusApplicationService applicationService(protocolService);
    api::ConsoleJsonApi api(applicationService);

    QTextStream input(stdin);
    QTextStream output(stdout);

    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        output << api.handleLine(line.toUtf8()) << Qt::endl;
    }

    return 0;
}
