project(xml_conformance)

add_executable(${PROJECT_NAME} ${PROJECT_NAME}.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE glz_test_common)

if (MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wa,-mbig-obj")
endif ()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

target_code_coverage(${PROJECT_NAME} AUTO ALL)

add_executable(${PROJECT_NAME}_struct ${PROJECT_NAME}_struct.cpp)
target_link_libraries(${PROJECT_NAME}_struct PRIVATE glz_test_common)

if (MINGW)
    target_compile_options(${PROJECT_NAME}_struct PRIVATE "-Wa,-mbig-obj")
endif ()

add_test(NAME ${PROJECT_NAME}_struct COMMAND ${PROJECT_NAME}_struct)
target_code_coverage(${PROJECT_NAME}_struct AUTO ALL)

# Data-driven test over the W3C XML Test Suite.
# Off by default to avoid downloading external data during normal builds.
option(glaze_XML_CONFORMANCE_DATA "Build data-driven W3C XML conformance test (downloads the suite)" OFF)
if (glaze_XML_CONFORMANCE_DATA)
    include(FetchContent)
    FetchContent_Declare(
        xmlconf
        URL https://www.w3.org/XML/Test/xmlts20130923.tar.gz
        URL_HASH SHA256=9b61db9f5dbffa545f4b8d78422167083a8568c59bd1129f94138f936cf6fc1f
    )
    FetchContent_MakeAvailable(xmlconf)

    add_executable(${PROJECT_NAME}_data ${PROJECT_NAME}_data.cpp)
    target_link_libraries(${PROJECT_NAME}_data PRIVATE glz_test_common)
    # The archive's top-level 'xmlconf/' directory is hoisted into xmlconf_SOURCE_DIR
    # itself by CMake's FetchContent extraction (single-root-directory archives are
    # unwrapped), so xmlconf_SOURCE_DIR *is* the xmlconf/ directory -- verified by
    # inspecting the extracted tree, which places xmltest/, sun/, ibm/, etc. directly
    # under xmlconf_SOURCE_DIR with no intervening xmlconf/ path component.
    target_compile_definitions(${PROJECT_NAME}_data PRIVATE
        XML_CONFORMANCE_DIR="${xmlconf_SOURCE_DIR}"
    )
    if (MINGW)
        target_compile_options(${PROJECT_NAME}_data PRIVATE "-Wa,-mbig-obj")
    endif ()
    add_test(NAME ${PROJECT_NAME}_data COMMAND ${PROJECT_NAME}_data)
    target_code_coverage(${PROJECT_NAME}_data AUTO ALL)
endif ()
