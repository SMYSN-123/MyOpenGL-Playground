cmake_minimum_required(VERSION 3.10)
project(MyGraphicsEngine)

# 1. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 2. 包含头文件路径 (告诉编译器去哪里找 .h)
# ${CMAKE_SOURCE_DIR} 代表当前项目根目录
include_directories(
    ${CMAKE_SOURCE_DIR}/extern/glfw/include
    ${CMAKE_SOURCE_DIR}/extern/glad/include
    ${CMAKE_SOURCE_DIR}/extern/glm
    ${CMAKE_SOURCE_DIR}/src 
)

# 3. 寻找库文件路径 (告诉链接器去哪里找 .lib)
link_directories(
    ${CMAKE_SOURCE_DIR}/extern/glfw/lib
)

# 4. 添加源文件 (包括 glad.c，它必须被编译！)
# 注意：glad.c 是 C 文件，所以项目可能需要混编
add_executable(MyGraphicsEngine 
    src/main.cpp 
    src/stb_image_setup.cpp
    extern/glad/src/glad.c
)

# 5. 链接库文件 (把 glfw3.lib 和 系统库 连进来)
# opengl32 是 Windows 自带的
target_link_libraries(MyGraphicsEngine 
    glfw3 
    opengl32
)