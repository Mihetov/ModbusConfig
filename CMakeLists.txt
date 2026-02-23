cmake_minimum_required(VERSION 3.16)

project(ModbusConfig LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS
    Core
    SerialPort
    SerialBus
    Network
)

add_subdirectory(domain)
add_subdirectory(core)
add_subdirectory(infrastructure)
add_subdirectory(app)
