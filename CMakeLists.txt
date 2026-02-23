cmake_minimum_required(VERSION 3.14)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.75.0 REQUIRED COMPONENTS system json)

set(MODBUS_SERIAL_COMMUNICATION OFF CACHE BOOL "Disable built-in Serial")
set(MODBUS_TCP_COMMUNICATION OFF CACHE BOOL "Disable built-in TCP")
add_subdirectory(MB/src MB_build EXCLUDE_FROM_ALL)

add_executable(${PROJECT_NAME}
    app/main.cpp
    layers/application/application_layer.cpp
    layers/application/application_layer.h
    layers/application/Device.h
    layers/application/Device.cpp
    layers/application/DeviceManager.h
    layers/application/DeviceManager.cpp
    layers/protocol/protocol_layer.cpp
    layers/protocol/protocol_layer.h
    layers/transport/transport_layer.cpp
    layers/transport/transport_layer.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::json
    Boost::system
    Modbus
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MB/include
)
