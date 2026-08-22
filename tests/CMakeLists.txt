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

add_executable(test_pwd_next_unit test_pwd_next_unit.cpp)
target_include_directories(test_pwd_next_unit PRIVATE ../src/producer)
target_link_libraries(test_pwd_next_unit PRIVATE producer_lib GTest::gtest_main)
gtest_discover_tests(test_pwd_next_unit)

add_executable(test_sha256 test_sha256.cpp)
target_link_libraries(test_sha256 PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_sha256)

add_executable(test_file_result_sink test_file_result_sink.cpp)
target_link_libraries(test_file_result_sink PRIVATE consumer_lib GTest::gtest_main)
gtest_discover_tests(test_file_result_sink)

add_executable(test_util test_util.cpp)
target_link_libraries(test_util PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_util)

add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE consumer_lib GTest::gtest_main)
gtest_discover_tests(test_thread_pool)

add_executable(test_echo test_echo.cpp)
target_link_libraries(test_echo PRIVATE producer_lib consumer_lib GTest::gtest_main)
gtest_discover_tests(test_echo)

add_executable(test_socket test_socket.cpp)
target_link_libraries(test_socket PRIVATE common GTest::gtest_main)
gtest_discover_tests(test_socket)
