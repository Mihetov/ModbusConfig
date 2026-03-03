cmake_minimum_required(VERSION 3.14)
project(ModbusConfig VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Платформозависимые настройки
if(WIN32)
    # Windows 7 support (0x0601 = Windows 7)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DWINVER=0x0601)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)
    
    add_definitions(-D_USE_MATH_DEFINES)

    if(NOT BOOST_ROOT)
        set(BOOST_ROOT "C:/local/boost_1_90_0" CACHE PATH "Boost root directory")
    endif()
    if(NOT BOOST_LIBRARYDIR)
        set(BOOST_LIBRARYDIR "C:/local/boost_1_90_0/lib64-msvc-14.3" CACHE PATH "Boost library directory")
    endif()

    if(MSVC)
        # СТАТИЧЕСКАЯ линковка для Windows 7
        set(Boost_USE_STATIC_LIBS ON)
        set(Boost_USE_STATIC_RUNTIME ON)
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_ARCHITECTURE "-x64")
        set(Boost_COMPILER "-vc143")
        set(BOOST_STATIC_DEFINES BOOST_ALL_NO_LIB)
        
        # ПРИНУДИТЕЛЬНО включаем статическую CRT (/MT) для ВСЕГО проекта
        # Удаляем все /MD и /MDd флаги
        foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif()
        endforeach()
        
        # Явно устанавливаем runtime библиотеку
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        
        # Дополнительные флаги для статической линковки CRT
        add_compile_options(/MT$<$<CONFIG:Debug>:d>)
    endif()

    set(Boost_NO_BOOST_CMAKE ON)
    set(Boost_NO_SYSTEM_PATHS ON)
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

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")
    target_sources(${PROJECT_NAME} PRIVATE resources.rc)
endif()

# Явно указываем, что это КОНСОЛЬНОЕ приложение
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# Дополнительные настройки для MSVC
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MANIFEST:NO"
    )
    
    # Принудительно устанавливаем статическую CRT для таргета
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")
    target_sources(${PROJECT_NAME} PRIVATE resources.rc)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MB/include
    ${Boost_INCLUDE_DIRS}
)

if(WIN32 AND MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${BOOST_STATIC_DEFINES})
endif()

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
        user32
        gdi32
        shell32
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

message(STATUS "Project configured successfully for Windows 7")
message(STATUS "  Boost version: ${Boost_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Boost linkage: STATIC")
message(STATUS "  CRT linkage: STATIC (/MT)")
message(STATUS "  Application type: CONSOLE")