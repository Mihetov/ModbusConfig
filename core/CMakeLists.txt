add_library(core
    CoreDummy.cpp
)

target_link_libraries(core
    PUBLIC domain
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
