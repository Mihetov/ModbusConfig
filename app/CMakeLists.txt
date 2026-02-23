add_executable(ModbusConfig
    main.cpp
)

target_link_libraries(ModbusConfig
    PRIVATE
        core
        infrastructure
        Qt5::Core
)

target_include_directories(ModbusConfig
    PRIVATE
        ${PROJECT_SOURCE_DIR}
)
