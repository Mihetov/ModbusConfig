#include "protocol_layer.h"
#include <stdexcept>

bool ProtocolHandler::jsonToMBCommand(const json::value& j, MBCommand& out, std::string& error) {
    // Код остается почти тот же, но используем std::string вместо QString
    if (!j.is_object()) {
        error = "Root must be JSON object";
        return false;
    }
    const json::object& obj = j.as_object();
    // Проверка обязательных полей
    if (obj.find("slave_id") == obj.end() ||
        obj.find("function") == obj.end() ||
        obj.find("address") == obj.end()) {
        error = "Missing required fields: slave_id, function, address";
        return false;
    }
    // Парсинг slave_id
    if (!obj.at("slave_id").is_int64() ||
        obj.at("slave_id").as_int64() < 0 ||
        obj.at("slave_id").as_int64() > 255) {
        error = "Invalid slave_id: must be integer between 0 and 255";
        return false;
    }
    out.slaveId = static_cast<uint8_t>(obj.at("slave_id").as_int64());
    // Парсинг function
    std::string funcName = obj.at("function").as_string().c_str();
    if (funcName == "read_holding") {
        out.functionCode = MB::utils::ReadAnalogOutputHoldingRegisters;
    } else if (funcName == "read_input") {
        out.functionCode = MB::utils::ReadAnalogInputRegisters;
    } else if (funcName == "write_single") {
        out.functionCode = MB::utils::WriteSingleAnalogOutputRegister;
    } else if (funcName == "write_multiple") {
        out.functionCode = MB::utils::WriteMultipleAnalogOutputHoldingRegisters;
    } else {
        error = "Unknown function: " + funcName;
        return false;
    }
    // Парсинг address
    if (!obj.at("address").is_int64()) {
        error = "Invalid address: must be integer";
        return false;
    }
    out.startAddress = static_cast<uint16_t>(obj.at("address").as_int64());
    // Парсинг count (опционально)
    if (obj.find("count") != obj.end()) {
        if (!obj.at("count").is_int64()) {
            error = "Invalid count: must be integer";
            return false;
        }
        out.count = static_cast<uint16_t>(obj.at("count").as_int64());
    } else {
        out.count = 1; // По умолчанию
    }
    // Парсинг values для записей (опционально)
    if (obj.find("values") != obj.end()) {
        const auto& values = obj.at("values");
        if (!values.is_array()) {
            error = "Invalid values: must be array";
            return false;
        }
        for (const auto& val : values.as_array()) {
            if (!val.is_int64()) {
                error = "Invalid value in array: must be integer";
                return false;
            }
            out.values.push_back(static_cast<uint16_t>(val.as_int64()));
        }
    }
    return true;
}

json::value ProtocolHandler::mbCommandToJson(const MBCommand& cmd, int requestId) {
    json::object result;
    result["jsonrpc"] = "2.0";
    result["id"] = requestId;

    json::object params;
    params["slave_id"] = static_cast<int64_t>(cmd.slaveId);
    params["function"] = MB::utils::mbFunctionToStr(cmd.functionCode);
    params["address"] = static_cast<int64_t>(cmd.startAddress);
    params["count"] = static_cast<int64_t>(cmd.count);

    result["params"] = params;
    return result;
}

std::vector<uint8_t> ProtocolHandler::createModbusPDU(const MBCommand& cmd) {
    try {
        // Создаем PDU вручную, без использования сторонней библиотеки
        std::vector<uint8_t> pdu;
        // Unit ID (1 байт)
        pdu.push_back(cmd.slaveId);
        // Function code (1 байт)
        pdu.push_back(static_cast<uint8_t>(cmd.functionCode));
        // Для READ-команд
        if (cmd.functionCode == MB::utils::ReadAnalogOutputHoldingRegisters ||
            cmd.functionCode == MB::utils::ReadAnalogInputRegisters) {
            // Start address (2 байта)
            pdu.push_back((cmd.startAddress >> 8) & 0xFF);
            pdu.push_back(cmd.startAddress & 0xFF);
            // Quantity (2 байта)
            pdu.push_back((cmd.count >> 8) & 0xFF);
            pdu.push_back(cmd.count & 0xFF);
        }
        // Для WRITE SINGLE
        else if (cmd.functionCode == MB::utils::WriteSingleAnalogOutputRegister) {
            if (cmd.values.empty()) {
                throw std::runtime_error("Values array is empty for write_single command");
            }
            // Register address (2 байта)
            pdu.push_back((cmd.startAddress >> 8) & 0xFF);
            pdu.push_back(cmd.startAddress & 0xFF);
            // Register value (2 байта)
            pdu.push_back((cmd.values[0] >> 8) & 0xFF);
            pdu.push_back(cmd.values[0] & 0xFF);
        }
        // Для WRITE MULTIPLE
        else if (cmd.functionCode == MB::utils::WriteMultipleAnalogOutputHoldingRegisters) {
            // Start address (2 байта)
            pdu.push_back((cmd.startAddress >> 8) & 0xFF);
            pdu.push_back(cmd.startAddress & 0xFF);
            // Quantity (2 байта)
            pdu.push_back((cmd.count >> 8) & 0xFF);
            pdu.push_back(cmd.count & 0xFF);
            // Byte count (1 байт)
            pdu.push_back(static_cast<uint8_t>(cmd.values.size() * 2));
            // Register values
            for (uint16_t value : cmd.values) {
                pdu.push_back((value >> 8) & 0xFF);
                pdu.push_back(value & 0xFF);
            }
        }
        else {
            throw std::runtime_error("Unsupported Modbus function code");
        }
        return pdu;
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create Modbus PDU: ") + e.what());
    }
}

MBResponse ProtocolHandler::parseModbusPDU(const std::vector<uint8_t>& pdu) {
    try {
        // ДОЛЖЕН БЫТЬ ПОЛНЫЙ РУЧНОЙ ПАРСИНГ PDU БЕЗ ИСПОЛЬЗОВАНИЯ СТОРОННЕЙ БИБЛИОТЕКИ
        if (pdu.size() < 2) {
            throw std::runtime_error("Invalid PDU: too short");
        }
        // Unit ID (1 байт)
        uint8_t unitId = pdu[0];
        // Function code (1 байт)
        uint8_t functionCode = pdu[1];

        MBResponse result;
        result.slaveId = unitId;
        result.functionCode = static_cast<MB::utils::MBFunctionCode>(functionCode);

        // Обработка в зависимости от функции
        if (functionCode == static_cast<uint8_t>(MB::utils::ReadAnalogOutputHoldingRegisters) ||
            functionCode == static_cast<uint8_t>(MB::utils::ReadAnalogInputRegisters)) {
            if (pdu.size() < 3) {
                throw std::runtime_error("Invalid PDU: missing byte count");
            }
            uint8_t byteCount = pdu[2];
            if (pdu.size() < 3 + byteCount) {
                throw std::runtime_error("Invalid PDU: incomplete data");
            }
            // Обработка регистров
            for (int i = 0; i < byteCount; i += 2) {
                uint16_t value = (pdu[3 + i] << 8) | pdu[4 + i];
                result.registerValues.push_back(value);
            }
        }
        // Другие функции обрабатываются аналогично...

        return result;
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse Modbus PDU: ") + e.what());
    }
}

json::value ProtocolHandler::processModbusPDU(const std::vector<uint8_t>& pdu, int& requestId) {
    try {
        MBResponse response = parseModbusPDU(pdu);

        json::object result;
        result["jsonrpc"] = "2.0";
        result["id"] = requestId++;

        json::object params;
        params["slave_id"] = static_cast<int64_t>(response.slaveId);
        params["function"] = MB::utils::mbFunctionToStr(response.functionCode);

        json::array values;
        for (uint16_t value : response.registerValues) {
            values.push_back(static_cast<int64_t>(value));
        }
        params["result"] = values;

        result["result"] = params;
        return result;
    }
    catch (const std::exception& e) {
        json::object error;
        error["jsonrpc"] = "2.0";
        error["id"] = requestId++;

        json::object errObj;
        errObj["code"] = -32603; // Internal error
        errObj["message"] = e.what();

        error["error"] = errObj;
        return error;
    }
}

json::value ProtocolHandler::processIncomingFrame(const std::vector<uint8_t>& frame, ConnectionType type, int& requestId) {
    try {
        // 1. Извлекаем PDU из фрейма
        std::vector<uint8_t> pdu;
        bool isRTU = (type == ConnectionType::RTU);

        if (isRTU) {
            // Проверяем CRC и извлекаем PDU
            if (frame.size() < 3) {
                throw std::runtime_error("Invalid RTU frame: too short");
            }
            // Проверяем CRC
            uint16_t calculatedCRC = 0xFFFF;
            for (size_t i = 0; i < frame.size() - 2; i++) {
                calculatedCRC ^= frame[i];
                for (int j = 0; j < 8; j++) {
                    if (calculatedCRC & 0x0001) {
                        calculatedCRC >>= 1;
                        calculatedCRC ^= 0xA001;
                    } else {
                        calculatedCRC >>= 1;
                    }
                }
            }

            uint16_t receivedCRC = (frame[frame.size() - 1] << 8) | frame[frame.size() - 2];
            if (calculatedCRC != receivedCRC) {
                throw std::runtime_error("Invalid CRC for RTU frame");
            }

            // Извлекаем PDU (без CRC)
            pdu = std::vector<uint8_t>(frame.begin(), frame.end() - 2);
        } else {
            // Извлекаем PDU из TCP фрейма
            if (frame.size() < 6) {
                throw std::runtime_error("Invalid TCP frame: too short");
            }
            uint16_t pduLength = (frame[4] << 8) | frame[5];
            if (frame.size() < 6 + pduLength) {
                throw std::runtime_error("Invalid TCP frame: incomplete data");
            }
            // Извлекаем PDU (без MBAP)
            pdu = std::vector<uint8_t>(frame.begin() + 6, frame.end());
        }

        // 2. Обрабатываем PDU
        return processModbusPDU(pdu, requestId);
    }
    catch (const std::exception& e) {
        json::object error;
        error["jsonrpc"] = "2.0";
        error["id"] = requestId++;

        json::object errObj;
        errObj["code"] = -32603; // Internal error
        errObj["message"] = e.what();

        error["error"] = errObj;
        return error;
    }
}

std::vector<uint8_t> ProtocolHandler::createModbusFrame(const MBCommand& cmd, ConnectionType type) {
    bool isRTU = (type == ConnectionType::RTU);
    try {
        // 1. Создаем PDU
        std::vector<uint8_t> pdu = createModbusPDU(cmd);
        // 2. Добавляем транспортную обертку
        std::vector<uint8_t> frame;

        if (isRTU) {
            // Добавляем CRC для RTU
            uint16_t crc = 0xFFFF;
            for (uint8_t byte : pdu) {
                crc ^= byte;
                for (int i = 0; i < 8; i++) {
                    if (crc & 0x0001) {
                        crc >>= 1;
                        crc ^= 0xA001;
                    } else {
                        crc >>= 1;
                    }
                }
            }

            frame = pdu;
            frame.push_back(crc & 0xFF);        // Низкий байт
            frame.push_back((crc >> 8) & 0xFF); // Высокий байт
        } else {
            // Добавляем MBAP для TCP
            std::vector<uint8_t> mbap(6, 0);
            // Transaction ID (2 байта)
            mbap[0] = 0x00;
            mbap[1] = 0x01;
            // Protocol ID (2 байта) - 0 для Modbus
            mbap[2] = 0x00;
            mbap[3] = 0x00;
            // Length (2 байта) - длина последующих байт (Unit ID + PDU)
            mbap[4] = ((pdu.size() + 1) >> 8) & 0xFF;
            mbap[5] = (pdu.size() + 1) & 0xFF;

            frame.reserve(mbap.size() + pdu.size());
            frame.insert(frame.end(), mbap.begin(), mbap.end());
            frame.insert(frame.end(), pdu.begin(), pdu.end());
        }

        return frame;
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create Modbus frame: ") + e.what());
    }
}

std::vector<json::value> ProtocolHandler::processIncomingBuffer(const std::vector<uint8_t>& buffer, ConnectionType type, int& requestId) {
    bool isRTU = (type == ConnectionType::RTU);
    std::vector<json::value> responses;

    if (isRTU) {
        // Добавляем новые данные в буфер RTU
        rtuBuffer_.insert(rtuBuffer_.end(), buffer.begin(), buffer.end());

        // Пытаемся извлечь полные фреймы из буфера
        while (rtuBuffer_.size() >= 3) { // Минимальный размер RTU фрейма
            // Проверяем, достаточно ли данных для полного фрейма
            if (rtuBuffer_.size() < 3) break;

            uint16_t calculatedCRC = 0xFFFF;
            for (size_t i = 0; i < rtuBuffer_.size() - 2; i++) {
                calculatedCRC ^= rtuBuffer_[i];
                for (int j = 0; j < 8; j++) {
                    if (calculatedCRC & 0x0001) {
                        calculatedCRC >>= 1;
                        calculatedCRC ^= 0xA001;
                    } else {
                        calculatedCRC >>= 1;
                    }
                }
            }

            uint16_t receivedCRC = (rtuBuffer_[rtuBuffer_.size() - 1] << 8) | rtuBuffer_[rtuBuffer_.size() - 2];
            if (calculatedCRC == receivedCRC) {
                // Извлекаем полный фрейм
                std::vector<uint8_t> frame(rtuBuffer_.begin(), rtuBuffer_.end());
                rtuBuffer_.clear();

                // Обрабатываем фрейм
                try {
                    responses.push_back(processIncomingFrame(frame, ConnectionType::RTU, requestId));
                } catch (const std::exception& e) {
                    std::cerr << "Error processing RTU frame: " << e.what() << std::endl;
                }
            } else {
                // Неполный фрейм, выходим из цикла
                break;
            }
        }
    } else {
        // Аналогично для TCP
        tcpBuffer_.insert(tcpBuffer_.end(), buffer.begin(), buffer.end());

        while (tcpBuffer_.size() >= 6) { // Минимальный размер TCP фрейма (MBAP заголовок)
            if (tcpBuffer_.size() < 6) break;

            uint16_t pduLength = (tcpBuffer_[4] << 8) | tcpBuffer_[5];
            if (tcpBuffer_.size() >= 6 + pduLength) {
                // Извлекаем полный фрейм
                std::vector<uint8_t> frame(tcpBuffer_.begin(), tcpBuffer_.begin() + 6 + pduLength);
                tcpBuffer_.erase(tcpBuffer_.begin(), tcpBuffer_.begin() + 6 + pduLength);

                // Обрабатываем фрейм
                try {
                    responses.push_back(processIncomingFrame(frame, ConnectionType::TCP, requestId));
                } catch (const std::exception& e) {
                    std::cerr << "Error processing TCP frame: " << e.what() << std::endl;
                }
            } else {
                // Неполный фрейм, выходим из цикла
                break;
            }
        }
    }

    return responses;
}

// Вспомогательные методы для обработки транспортной обертки

std::vector<uint8_t> ProtocolHandler::createMBAPHeader(uint16_t length) {
    std::vector<uint8_t> header(6, 0);
    // Transaction ID (2 байта) - в реальной реализации должен быть уникальным
    header[0] = 0x00;
    header[1] = 0x01;
    // Protocol ID (2 байта) - 0 для Modbus
    header[2] = 0x00;
    header[3] = 0x00;
    // Length (2 байта) - длина последующих байт (Unit ID + PDU)
    header[4] = (length >> 8) & 0xFF;
    header[5] = length & 0xFF;
    return header;
}

std::vector<uint8_t> ProtocolHandler::addCRC(const std::vector<uint8_t>& data) {
    // Вычисляем CRC-16 для данных
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001; // Полином CRC-16 Modbus
            } else {
                crc >>= 1;
            }
        }
    }

    // Добавляем CRC в конец данных
    std::vector<uint8_t> result = data;
    result.push_back(crc & 0xFF);        // Низкий байт
    result.push_back((crc >> 8) & 0xFF); // Высокий байт
    return result;
}

bool ProtocolHandler::validateCRC(const std::vector<uint8_t>& frame) {
    if (frame.size() < 3) {
        return false; // Недостаточно данных для проверки CRC
    }

    // Вычисляем CRC для всех байт, кроме последних двух
    uint16_t calculatedCRC = 0xFFFF;
    for (size_t i = 0; i < frame.size() - 2; i++) {
        calculatedCRC ^= frame[i];
        for (int j = 0; j < 8; j++) {
            if (calculatedCRC & 0x0001) {
                calculatedCRC >>= 1;
                calculatedCRC ^= 0xA001;
            } else {
                calculatedCRC >>= 1;
            }
        }
    }

    // Получаем ожидаемый CRC из последних двух байт
    uint16_t receivedCRC = (frame[frame.size() - 1] << 8) | frame[frame.size() - 2];

    return calculatedCRC == receivedCRC;
}
