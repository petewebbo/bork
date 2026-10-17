include(FetchContent)
FetchContent_Declare(SPSCQueue
    GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
    GIT_TAG 565a5149d54930463d58cb0f69b978d439555e66 # v1.1
)
FetchContent_MakeAvailable(SPSCQueue)
