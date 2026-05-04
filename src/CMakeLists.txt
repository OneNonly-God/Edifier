file(GLOB_RECURSE SRC
     CONFIGURE_DEPENDS
     "*.cpp" 
     "*.h"
)


if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
endif()

add_executable(Edifier ${SRC})

target_include_directories(Edifier PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(UNIX AND NOT APPLE)
    target_include_directories(Edifier PUBLIC ${GTK3_INCLUDE_DIRS})
    target_compile_options(Edifier PRIVATE ${GTK3_CFLAGS_OTHER})
endif()

target_link_libraries(Edifier PUBLIC
    glfw
    glad
    imgui
)

if(UNIX AND NOT APPLE)
    target_link_libraries(Edifier PUBLIC ${GTK3_LIBRARIES})
endif()