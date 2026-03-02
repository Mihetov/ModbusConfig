cmake_minimum_required(VERSION 3.21)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опция для XP
option(WINXP "Build for Windows XP" OFF)

# Определяем архитектуру из переменной окружения или автоматически
if(DEFINED ENV{ARCH})
    set(ARCH "$ENV{ARCH}")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH "x86")
else()
    set(ARCH "x64")  # По умолчанию x64
endif()

message(STATUS "Building for architecture: ${ARCH}")

# Устанавливаем правильные имена для платформы
if(ARCH STREQUAL "x86")
    set(PLATFORM_NAME "Win32")
    set(BOOST_ARCH_SUFFIX "32")
    set(BOOST_ARCH_PARAM "-x32")  # ВАЖНО: для вашего Boost нужно -x32, не -x86
elseif(ARCH STREQUAL "x64")
    set(PLATFORM_NAME "x64")
    set(BOOST_ARCH_SUFFIX "64")
    set(BOOST_ARCH_PARAM "-x64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${ARCH}")
endif()


# Платформозависимые настройки
if(WIN32)
    # Явно указываем статическую линковку с CRT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    if(WINXP)
        # Настройки для Windows XP (0x0501)
        set(WINVER_VALUE 0x0501)
        set(MSG "Windows XP (${ARCH})")
    else()
        # Настройки для Windows 7 (0x0601)
        set(WINVER_VALUE 0x0601)
        set(MSG "Windows 7+ (${ARCH})")
    endif()
    
    add_definitions(-D_WIN32_WINNT=${WINVER_VALUE})
    add_definitions(-DWINVER=${WINVER_VALUE})
    add_definitions(-DNTDDI_VERSION=${WINVER_VALUE}0000)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)

    # Настройки Boost
    if(NOT BOOST_ROOT)
        if(WINXP)
            set(BOOST_ROOT "C:/local/boost_1_73_0" CACHE PATH "Boost root directory (XP)")
        else()
            set(BOOST_ROOT "C:/local/boost_1_90_0" CACHE PATH "Boost root directory")
        endif()
    endif()
    
    if(MSVC)
        set(Boost_USE_STATIC_LIBS ON)      
        set(Boost_USE_STATIC_RUNTIME ON)   
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_NO_BOOST_CMAKE ON)
        set(Boost_NO_SYSTEM_PATHS ON)
        
        if(WINXP)
            # Для XP с Boost 1.73 используем vc120 (VS 2013)
            set(Boost_COMPILER "-vc120")
            # Библиотеки в lib32-msvc-12.0 (не stage/lib)
            set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib32-msvc-12.0" CACHE PATH "Boost library directory (XP)")
        else()
            # Для Win7 используем vc143 (VS 2022)
            set(Boost_COMPILER "-vc143")
            set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib${BOOST_ARCH_SUFFIX}-msvc-14.3" CACHE PATH "Boost library directory")
        endif()
        
        # Устанавливаем архитектуру для Boost
        set(Boost_ARCHITECTURE "${BOOST_ARCH_PARAM}")
    endif()
else()
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
endif()

# Поиск Boost
if(WINXP)
    # Для XP используем Boost 1.73
    find_package(Boost 1.73.0 REQUIRED COMPONENTS json system)
else()
    # Для Win7 используем Boost 1.75+
    find_package(Boost 1.75.0 REQUIRED COMPONENTS json)
endif()

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

# Явно указываем версии Windows API для таргета
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_GLOBAL_Manifest "true"
        VS_DPI_AWARE "PerMonitor"
    )
    
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=${WINVER_VALUE}
        WINVER=${WINVER_VALUE}
        NTDDI_VERSION=${WINVER_VALUE}0000
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        BOOST_USE_WINAPI_VERSION=${WINVER_VALUE}
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

# Линковка
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
    )

    # Вывод exe в папку bin с учётом архитектуры
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${ARCH}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/${ARCH}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/${ARCH}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/${ARCH}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/${ARCH}"
    )
endif()

message(STATUS "Project configured successfully")
message(STATUS "  Architecture: ${ARCH}")
message(STATUS "  Platform: ${PLATFORM_NAME}")
message(STATUS "  Boost version: ${Boost_VERSION}")
message(STATUS "  Boost library dir: ${BOOST_LIBRARYDIR}")
message(STATUS "  Boost compiler: ${Boost_COMPILER}")
message(STATUS "  Boost architecture: ${Boost_ARCHITECTURE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Target Windows: ${MSG}")
message(STATUS "  Runtime: Static (/MT)")
message(STATUS "  WINXP option: ${WINXP}")