#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <boost/json.hpp>
#include "MB/include/modbusRequest.hpp"
#include "MB/include/modbusResponse.hpp"
#include "MB/include/modbusUtils.hpp"

enum class ConnectionType {
    TCP,
    RTU
};

namespace json = boost::json;

// DTO для Modbus-команд
struct MBCommand {
    uint8_t slaveId;
    MB::utils::MBFunctionCode functionCode;
    uint16_t startAddress;
    uint16_t count;
    std::vector<uint16_t> values; // Для записей
};

struct MBResponse {
    uint8_t slaveId;
    MB::utils::MBFunctionCode functionCode;
    std::vector<uint16_t> registerValues;
};

class ProtocolHandler {
public:
    ProtocolHandler() = default;

    // Обработка входящего фрейма (полный фрейм с транспортной оберткой)
    json::value processIncomingFrame(const std::vector<uint8_t>& frame, ConnectionType type, int& requestId);

    // Создание ПОЛНОГО фрейма (с транспортной оберткой)
    std::vector<uint8_t> createModbusFrame(const MBCommand& cmd, ConnectionType type);

    // Обработка входящего буфера (может содержать несколько фреймов или неполный фрейм)
    std::vector<json::value> processIncomingBuffer(const std::vector<uint8_t>& buffer, ConnectionType type, int& requestId);

    // Преобразование JSON в Modbus-команду
    bool jsonToMBCommand(const json::value&, MBCommand&, std::string& error);

    // Преобразование Modbus-команды в JSON
    json::value mbCommandToJson(const MBCommand& cmd, int requestId);

    // Создает только PDU (без транспортной обертки)
    std::vector<uint8_t> createModbusPDU(const MBCommand& cmd);

    // Парсит только PDU (без транспортной обертки)
    MBResponse parseModbusPDU(const std::vector<uint8_t>& pdu);

    // Обработка входящего PDU (уже без транспортной обертки)
    json::value processModbusPDU(const std::vector<uint8_t>& pdu, int& requestId);

private:
    // Буферы для неполных фреймов
    std::vector<uint8_t> tcpBuffer_;
    std::vector<uint8_t> rtuBuffer_;
    int currentRequestId = 0;

    // Вспомогательные методы для обработки транспортной обертки
    std::vector<uint8_t> createMBAPHeader(uint16_t length);
    std::vector<uint8_t> addCRC(const std::vector<uint8_t>& pdu);
    bool validateCRC(const std::vector<uint8_t>& frame);
};
