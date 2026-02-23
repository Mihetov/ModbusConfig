cmake_minimum_required(VERSION 3.14)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)  # ВАЖНО: включаем автоматическую обработку Q_OBJECT

find_package(Qt5 COMPONENTS Core Network SerialPort REQUIRED)

# Ищем Boost с компонентами system и json
find_package(Boost 1.75.0 REQUIRED COMPONENTS system json)

set(MODBUS_SERIAL_COMMUNICATION OFF CACHE BOOL "Disable built-in Serial")
set(MODBUS_TCP_COMMUNICATION OFF CACHE BOOL "Disable built-in TCP")
add_subdirectory(MB/src MB_build EXCLUDE_FROM_ALL)

add_executable(${PROJECT_NAME}
    app/main.cpp
    layers/application/application_layer.cpp
    layers/application/application_layer.h
    layers/api/api_layer.cpp
    layers/api/api_layer.h
    layers/protocol/protocol_layer.cpp
    layers/protocol/protocol_layer.h
    layers/transport/transport_layer.cpp
    layers/transport/transport_layer.h
    layers/transport/ITransport.h
    layers/protocol/ModbusTypes.h
    layers/protocol/IModbusCodec.h
    layers/protocol/ModbusRtuCodec.cpp
    layers/protocol/ModbusTcpCodec.cpp
    layers/application/Device.h
    layers/application/DeviceManager.h
    layers/application/Device.cpp
)

# Линкуем Boost.JSON и Boost.System
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Network
    Qt5::SerialPort
    Boost::json
    Boost::system
    Modbus
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MB/include
    ${CMAKE_CURRENT_SOURCE_DIR}/layers
)
