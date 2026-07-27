project(xml_test)

add_executable(${PROJECT_NAME} ${PROJECT_NAME}.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE glz_test_common)

if (MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE "-Wa,-mbig-obj")
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    GLZ_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

target_code_coverage(${PROJECT_NAME} AUTO ALL)

add_executable(xml_schema_test xml_schema_test.cpp)
target_link_libraries(xml_schema_test PRIVATE glz_test_common)

if (MINGW)
    target_compile_options(xml_schema_test PRIVATE "-Wa,-mbig-obj")
endif ()

add_test(NAME xml_schema_test COMMAND xml_schema_test)
target_code_coverage(xml_schema_test AUTO ALL)
