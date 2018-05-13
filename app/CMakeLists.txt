include_directories(live555/liveMedia/include)
include_directories(live555/BasicUsageEnvironment/include)
include_directories(live555/groupsock/include)
include_directories(live555/UsageEnvironment/include)


cmake_minimum_required(VERSION 3.4.1)
add_library(
    rtspclient SHARED
    src/main/cpp/rtspclient.cpp
)

find_library(log-lib log)

target_link_libraries(
    rtspclient
    ${log-lib}
)

add_subdirectory(src/main/cpp/live555)