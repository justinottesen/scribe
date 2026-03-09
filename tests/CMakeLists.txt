find_package(GTest REQUIRED)

add_executable(scribe.test)
target_sources(scribe.test PRIVATE scribe.test.cpp)
target_link_libraries(
    scribe.test
    PRIVATE scribe GTest::gtest GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(scribe.test)
