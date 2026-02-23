add_library(infrastructure
    InfrastructureDummy.cpp
)

target_link_libraries(infrastructure
    PUBLIC
        domain
        Qt5::Core
        Qt5::SerialPort
        Qt5::SerialBus
        Qt5::Network
)

target_include_directories(infrastructure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
