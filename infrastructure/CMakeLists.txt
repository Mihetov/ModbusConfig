add_library(infrastructure
    transport/InMemoryModbusTransport.cpp
    transport/MazurelModbusTransport.cpp
)

target_link_libraries(infrastructure
    PUBLIC
        core
        domain
        Qt5::Core
        Qt5::SerialPort
        Qt5::SerialBus
        Qt5::Network
)

target_include_directories(infrastructure
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)
