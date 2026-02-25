cmake_minimum_required(VERSION 3.14)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Платформозависимые настройки
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)  # Windows 10
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)

    if(NOT BOOST_ROOT)
        set(BOOST_ROOT "C:/local/boost_1_90_0" CACHE PATH "Boost root directory")
    endif()
    if(NOT BOOST_LIBRARYDIR)
        set(BOOST_LIBRARYDIR "C:/local/boost_1_90_0/lib64-msvc-14.3" CACHE PATH "Boost library directory")
    endif()

    if(MSVC)
        set(Boost_USE_STATIC_LIBS OFF)
        set(Boost_USE_STATIC_RUNTIME OFF)
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_ARCHITECTURE "-x64")
        set(Boost_COMPILER "-vc143")
        set(BOOST_DYNAMIC_DEFINES BOOST_ALL_DYN_LINK BOOST_JSON_DYN_LINK BOOST_ALL_NO_LIB)
    endif()

    set(Boost_NO_BOOST_CMAKE ON)
    set(Boost_NO_SYSTEM_PATHS ON)
else()
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
endif()

# Поиск Boost (json обязателен, system — header-only)
find_package(Boost 1.75.0 REQUIRED COMPONENTS json)

# Alias для Boost::system (на случай, если нужен)
if(NOT TARGET Boost::system)
    add_library(Boost::system INTERFACE IMPORTED)
    target_link_libraries(Boost::system INTERFACE)
    target_include_directories(Boost::system INTERFACE ${Boost_INCLUDE_DIRS})
endif()

# Подпроект Modbus
set(MODBUS_SERIAL_COMMUNICATION OFF CACHE BOOL "Disable built-in Serial")
set(MODBUS_TCP_COMMUNICATION OFF CACHE BOOL "Disable built-in TCP")
add_subdirectory(MB/src MB_build EXCLUDE_FROM_ALL)

# Основная программа
add_executable(${PROJECT_NAME}
    app/main.cpp
    layers/application/application_layer.cpp
    layers/application/application_layer.h
    layers/application/Device.h
    layers/application/Device.cpp
    layers/application/DeviceManager.h
    layers/application/DeviceManager.cpp
    layers/api/api_layer.cpp
    layers/api/api_layer.h
    layers/protocol/protocol_layer.cpp
    layers/protocol/protocol_layer.h
    layers/transport/transport_layer.cpp
    layers/transport/transport_layer.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MB/include
    ${Boost_INCLUDE_DIRS}
)

if(WIN32 AND MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${BOOST_DYNAMIC_DEFINES})
endif()

# Линковка с Modbus_Core (вместо Modbus)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::json
    Boost::system
    Modbus_Core   # <--- ИЗМЕНЕНО
)

# Windows: системные библиотеки и копирование DLL
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        iphlpapi
        advapi32
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying DLLs to ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
        # Boost DLL (debug)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BOOST_LIBRARYDIR}/boost_json-vc143-mt-gd-x64-1_90.dll"
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/" 2>NUL || (exit 0)
        # Boost DLL (release)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BOOST_LIBRARYDIR}/boost_json-vc143-mt-x64-1_90.dll"
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/" 2>NUL || (exit 0)
        # Modbus_Core.dll (если вдруг есть)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/MB_build/$<CONFIG>/Modbus_Core.dll"
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/" 2>NUL || (exit 0)
        COMMENT "Copying DLLs to output directory (warnings ignored)"
    )
endif()

message(STATUS "Project configured successfully")
message(STATUS "  Boost version: ${Boost_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")