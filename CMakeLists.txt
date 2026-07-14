cmake_minimum_required(VERSION 3.16)
project(vulkan-reference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt MOC 自动处理
set(CMAKE_AUTOMOC ON)



# Qt 5.12.12 
set(CMAKE_PREFIX_PATH "D:/qt5.12.12/msvc2022_64")
find_package(Qt5 5.12.12 REQUIRED COMPONENTS Core Widgets Gui)

# Vulkan SDK
find_package(Vulkan REQUIRED)


set(SOURCES
    MainWindow.cpp
    QWindowVulkan.cpp
    VulkanRender.cpp
    VulkanObject.cpp
    VulkanLayer.cpp
    CEditLine.cpp
)

set(HEADERS
    MainWindow.h
    QWindowVulkan.h
    VulkanRender.h
    VulkanObject.h
    VulkanLayer.h
    CEditLine.h
)



add_executable(vulkan-reference
    main.cpp
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(vulkan-reference PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vulkan-reference
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Vulkan::Vulkan
)



set(SHADER_SPV
    shaders/vert.spv
    shaders/frag.spv
)

foreach(spv ${SHADER_SPV})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${spv}
        ${CMAKE_CURRENT_BINARY_DIR}/${spv}
        COPYONLY
    )
endforeach()



# 设置工作目录为输出目录
set_target_properties(vulkan-reference PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan-reference>"
)


set_target_properties(vulkan-reference PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan-reference>"
    VS_DEBUGGER_ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=D:/qt5.12.12/msvc2022_64/plugins/platforms"
)