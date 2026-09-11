cmake_minimum_required(VERSION 3.17)

if(NOT DEFINED ACBT_PROJECT_LOADED)
    include(cmake/project.cmake)
endif()

project(auik-ext-plot
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "Auik plot extension")

if(NOT DEFINED ACBT_LOADED)
    include(cmake/utils.cmake)
endif()

create_symbol_export_file()

if(NOT COMMAND compile_auik_style_ids)
    include("${CMAKE_CURRENT_SOURCE_DIR}/modules/auik/tools/theme_build.cmake")
endif()

set(AUIK_EXT_PLOT_BASE_THEME "${CMAKE_CURRENT_SOURCE_DIR}/src/style/theme.asd")
set(AUIK_EXT_PLOT_STYLE_IDS_CSV "${CMAKE_CURRENT_SOURCE_DIR}/src/style/default_style_tags_id.csv")
set(AUIK_EXT_PLOT_STYLE_IDS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/include/auik/ext/plot/widget_tags.hpp")
set(AUIK_EXT_PLOT_STYLE_IDS_TARGET "auik_ext_plot_default_style_ids")

compile_auik_style_ids("${AUIK_EXT_PLOT_BASE_THEME}"
    IDS_CSV "${AUIK_EXT_PLOT_STYLE_IDS_CSV}"
    IDS_HEADER "${AUIK_EXT_PLOT_STYLE_IDS_HEADER}"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/include"
    TARGET_NAME "${AUIK_EXT_PLOT_STYLE_IDS_TARGET}"
)

add_files(${PROJECT_NAME} RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/src")
list(APPEND AUIK_EXT_PLOT_SRC ${AUIK_EXT_PLOT_STYLE_IDS_HEADER})
add_library(${PROJECT_NAME} SHARED ${AUIK_EXT_PLOT_SRC})
add_dependencies(${PROJECT_NAME} ${AUIK_EXT_PLOT_STYLE_IDS_TARGET})

set_target_properties(${PROJECT_NAME} PROPERTIES
    DEFINE_SYMBOL AUIK_EXT_PLOT_BUILD
    CXX_VISIBILITY_PRESET hidden
)

target_link_libraries(${PROJECT_NAME} PUBLIC acul auik)

target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${AUIK_EXT_PLOT_CONFIGURE_PATH}"
)

if(WIN32)
    gen_manifest_lib(lib${PROJECT_NAME} ${PROJECT_VERSION})
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_EXTENSIONS YES
)

get_directory_property(AUIK_EXT_PLOT_HAS_PARENT PARENT_DIRECTORY)
if(AUIK_EXT_PLOT_HAS_PARENT)
    set(AUIK_EXT_PLOT_STYLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/style" PARENT_SCOPE)
    set(AUIK_EXT_PLOT_STYLE_IDS_CSV "${AUIK_EXT_PLOT_STYLE_IDS_CSV}" PARENT_SCOPE)
endif()
