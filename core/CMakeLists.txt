add_library(core
    api/ConsoleJsonApi.cpp
    application/ModbusApplicationService.cpp
    protocol/ModbusProtocolService.cpp
)

target_link_libraries(core
    PUBLIC
        domain
        Qt5::Core
)

target_include_directories(core
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
)
