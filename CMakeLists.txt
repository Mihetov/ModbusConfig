cmake_minimum_required(VERSION 3.21)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Платформозависимые настройки
if(WIN32)
    # Явно указываем статическую линковку с CRT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # Альтернативный способ для старых версий CMake:
    # add_compile_options($<$<CONFIG:Release>:/MT> $<$<CONFIG:Debug>:/MTd>)
    
    # 0x0601 = Windows 7
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DWINVER=0x0601)
    add_definitions(-DNTDDI_VERSION=0x06010000)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)

    # Настройки Boost - ВСЁ СТАТИЧЕСКИ
    if(NOT BOOST_ROOT)
        set(BOOST_ROOT "C:/local/boost_1_90_0" CACHE PATH "Boost root directory")
    endif()
    
    if(MSVC)
        set(Boost_USE_STATIC_LIBS ON)      
        set(Boost_USE_STATIC_RUNTIME ON)   
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_ARCHITECTURE "-x64")
        set(Boost_COMPILER "-vc143")
        
        # Убираем любые намёки на динамическую линковку
        # НЕ определяем BOOST_ALL_DYN_LINK и подобные
    endif()

    set(Boost_NO_BOOST_CMAKE ON)
    set(Boost_NO_SYSTEM_PATHS ON)
    
    # Указываем директорию с библиотеками (статические .lib)
    set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib64-msvc-14.3" CACHE PATH "Boost library directory")
else()
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
endif()

# Поиск Boost
find_package(Boost 1.75.0 REQUIRED COMPONENTS json)

# Alias для Boost::system
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

# Явно указываем версии Windows API
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_GLOBAL_Manifest "true"
        VS_DPI_AWARE "PerMonitor"
    )
    
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0601
        WINVER=0x0601
        NTDDI_VERSION=0x06010000
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        BOOST_USE_WINAPI_VERSION=0x0601
    )
endif()

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE resources.rc)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MB/include
    ${Boost_INCLUDE_DIRS}
)

# Линковка - ТОЛЬКО СТАТИЧЕСКИЕ БИБЛИОТЕКИ
target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::json
    Boost::system
    Modbus_Core
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        iphlpapi
        advapi32
        # Явно указываем статические runtime библиотеки (на всякий случай)
        # Обычно CMake добавляет их автоматически при /MT
    )

    # Вывод exe в папку bin
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
    )
endif()

message(STATUS "Project configured successfully")
message(STATUS "  Boost version: ${Boost_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Target Windows: 7+")
message(STATUS "  Runtime: Static (/MT)")