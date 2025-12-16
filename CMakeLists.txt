cmake_minimum_required(VERSION 3.15)

project(final_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_PATH ${CMAKE_SOURCE_DIR}/src)
set(THIRD_PARTY_LIBRARY_PATH ${CMAKE_SOURCE_DIR}/external)

# Copy data files into the build folder
file(COPY "media/" DESTINATION "media")

# Compile third party libraries
add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/glad)
add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/glfw)
add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/glm)
# add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/imgui)
add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/stb)
add_subdirectory(${THIRD_PARTY_LIBRARY_PATH}/tinygltf)

set(IMGUI_DIR ${THIRD_PARTY_LIBRARY_PATH}/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw glad)

add_library(ImGuiFileDialog STATIC
    ${THIRD_PARTY_LIBRARY_PATH}/ImGuiFileDialog/ImGuiFileDialog.cpp
)

target_include_directories(ImGuiFileDialog PUBLIC 
    ${THIRD_PARTY_LIBRARY_PATH}/ImGuiFileDialog
    ${THIRD_PARTY_LIBRARY_PATH}/imgui 
)

# Set src code
file(GLOB BASE_HDR ${SOURCE_PATH}/base/*.h)
file(GLOB BASE_SRC ${SOURCE_PATH}/base/*.cpp)
file(GLOB PROJECT_HDR ${SOURCE_PATH}/*.h)
file(GLOB PROJECT_SRC ${SOURCE_PATH}/*.cpp)

# temp
file(GLOB ENGINE_HDR ${SOURCE_PATH}/engine/*.h)
file(GLOB ENGINE_SRC ${SOURCE_PATH}/engine/*.cpp)

# Generate program
add_executable(final_project ${BASE_HDR} ${BASE_SRC} ${PROJECT_HDR} ${PROJECT_SRC} ${ENGINE_HDR} ${ENGINE_SRC})

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
elseif(UNIX)
if(CMAKE_BUILD_TYPE MATCHES Debug)
    set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Debug")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/Release")
endif()
endif()

# Include third party header files
target_include_directories(
    final_project PUBLIC
    ${SOURCE_PATH}
    ${THIRD_PARTY_LIBRARY_PATH}/glad/include
    ${THIRD_PARTY_LIBRARY_PATH}/glfw/include
    ${THIRD_PARTY_LIBRARY_PATH}/glm
    ${THIRD_PARTY_LIBRARY_PATH}/imgui
    ${THIRD_PARTY_LIBRARY_PATH}/imgui/backends
    ${THIRD_PARTY_LIBRARY_PATH}/stb
    ${THIRD_PARTY_LIBRARY_PATH}/tinygltf
    ${THIRD_PARTY_LIBRARY_PATH}/ImGuiFileDialog
)

# Link third party libraries
target_link_libraries(final_project PUBLIC glad glfw glm imgui stb tinygltf ImGuiFileDialog)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    
    target_link_libraries(imgui PUBLIC ${X11_LIBRARIES})
    
    # 有时候还需要链接 dl 和 pthread，虽然 GLFW 可能已经处理了，但为了保险可以加上
    # target_link_libraries(final_project PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
