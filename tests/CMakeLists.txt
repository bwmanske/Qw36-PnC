include(GoogleTest)

enable_testing()

add_executable(test_message test_message.cpp)
target_link_libraries(test_message PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_message)

add_executable(test_queue test_queue.cpp)
target_link_libraries(test_queue PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_queue)

add_executable(test_work_tracker test_work_tracker.cpp)
target_link_libraries(test_work_tracker PRIVATE producer_lib GTest::gtest_main)
gtest_discover_tests(test_work_tracker)

add_executable(test_checkpoint test_checkpoint.cpp)
target_link_libraries(test_checkpoint PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_checkpoint)

add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE producer_lib consumer_lib GTest::gtest_main)
gtest_discover_tests(test_integration)
