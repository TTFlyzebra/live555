cmake_minimum_required(VERSION 3.4.1)

include_directories(src/main/cpp/live555/liveMedia/include)
include_directories(src/main/cpp/live555/BasicUsageEnvironment/include)
include_directories(src/main/cpp/live555/groupsock/include)
include_directories(src/main/cpp/live555/UsageEnvironment/include)

file(GLOB SOURCE_CPP_FILE
    "src/main/cpp/live555/liveMedia/*.cpp"
    "src/main/cpp/live555/liveMedia/*.c"
    "src/main/cpp/live555/BasicUsageEnvironment/*.cpp"
    "src/main/cpp/live555/groupsock/*.cpp"
    "src/main/cpp/live555/groupsock/*.c"
    "src/main/cpp/live555/UsageEnvironment/*.cpp"
    "src/main/cpp/RtspClientJNI.cpp"
)
add_library(
    rtspclient
    SHARED
    ${SOURCE_CPP_FILE}
)

find_library(
    log-lib
    log
)

target_link_libraries(
    rtspclient
    ${log-lib}
)