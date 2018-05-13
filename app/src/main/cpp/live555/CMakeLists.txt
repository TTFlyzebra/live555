file(GLOB native_srcs
    "liveMedia/*.cpp"
    "liveMedia/*.c"
    "BasicUsageEnvironment/*.cpp"
    "groupsock/*.cpp"
    "groupsock/*.c"
    "UsageEnvironment/*.cpp"
)
add_library(
    live555
    SHARED
    ${native_srcs}
)

target_link_libraries(
    live555
    ${log-lib}
)