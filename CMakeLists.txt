cmake_minimum_required(VERSION 3.16)

project(EquipmentBurnInMonitor VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json：clangd / clang-tidy 等工具靠它定位编译选项
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt 安装路径：仅在没有外部指定时填本机默认，避免把个人路径写成硬约束。
# 换机器或 CI 上用 -DCMAKE_PREFIX_PATH=<Qt 安装前缀> 覆盖即可。
if(NOT DEFINED CMAKE_PREFIX_PATH AND NOT DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT DEFINED Qt6_DIR)
    set(CMAKE_PREFIX_PATH "E:/Qt/6.10.0/mingw_64" CACHE PATH "Qt6 prefix")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql PrintSupport Network)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR})

# ════════════════════════════════════════════════════════════════════════
# 源文件只有这一处清单。
# 新增/删除文件只改这里 —— 之前文件分散在 PROJECT_SOURCES 与 qt_add_executable
# 两处，DiagnosticsReporter 就是这样漏掉、长期不参与编译的。
# ════════════════════════════════════════════════════════════════════════
set(PROJECT_SOURCES
    # ── 入口 ──
    ${SRC}/main.cpp

    # ── 展示层 ──
    ${SRC}/MainWindow.h             ${SRC}/MainWindow.cpp
    ${SRC}/pages/HomePage.h         ${SRC}/pages/HomePage.cpp
    ${SRC}/pages/SettingsPage.h     ${SRC}/pages/SettingsPage.cpp
    ${SRC}/pages/VideoPage.h        ${SRC}/pages/VideoPage.cpp

    # ── 数据模型 ──
    ${SRC}/EquipmentData.h          ${SRC}/EquipmentData.cpp
    ${SRC}/EquipmentDataProvider.h  ${SRC}/EquipmentDataProvider.cpp

    # ── 基础设施 ──
    ${SRC}/src/logging/Logger.h     ${SRC}/src/logging/Logger.cpp

    # ── 配置层 ──
    ${SRC}/src/config/Config.h          ${SRC}/src/config/Config.cpp
    ${SRC}/src/config/ConfigWatcher.h   ${SRC}/src/config/ConfigWatcher.cpp

    # ── 通信层 ──
    ${SRC}/src/modbus/ModbusTypes.h     ${SRC}/src/modbus/ModbusTypes.cpp
    ${SRC}/src/modbus/ModbusTcpClient.h ${SRC}/src/modbus/ModbusTcpClient.cpp
    ${SRC}/src/modbus/ModbusSession.h   ${SRC}/src/modbus/ModbusSession.cpp

    # ── 设备抽象层 ──
    ${SRC}/src/device/DeviceDriver.h
    ${SRC}/src/device/ModbusDeviceDriver.h      ${SRC}/src/device/ModbusDeviceDriver.cpp
    ${SRC}/src/device/MotorTemperatureDevice.h  ${SRC}/src/device/MotorTemperatureDevice.cpp
    ${SRC}/src/device/CurrentDevice.h           ${SRC}/src/device/CurrentDevice.cpp
    ${SRC}/src/device/SimpleRegisterDevice.h    ${SRC}/src/device/SimpleRegisterDevice.cpp

    # ── 调度层 ──
    ${SRC}/src/scheduler/PollingScheduler.h ${SRC}/src/scheduler/PollingScheduler.cpp

    # ── 规则层 ──
    ${SRC}/src/rules/Rule.h
    ${SRC}/src/rules/RuleEngine.h       ${SRC}/src/rules/RuleEngine.cpp
    ${SRC}/src/rules/ThresholdRule.h    ${SRC}/src/rules/ThresholdRule.cpp
    ${SRC}/src/rules/RateChangeRule.h   ${SRC}/src/rules/RateChangeRule.cpp

    # ── 服务门面 ──
    ${SRC}/src/api/ServiceFacade.h      ${SRC}/src/api/ServiceFacade.cpp

    # ── 数据层 ──
    ${SRC}/src/data/DataCache.h         ${SRC}/src/data/DataCache.cpp
    ${SRC}/src/data/SqliteRepository.h  ${SRC}/src/data/SqliteRepository.cpp

    # ── 诊断层 ──
    ${SRC}/src/diagnostics/HealthMonitor.h        ${SRC}/src/diagnostics/HealthMonitor.cpp
    ${SRC}/src/diagnostics/DiagnosticsReporter.h  ${SRC}/src/diagnostics/DiagnosticsReporter.cpp
    ${SRC}/src/metrics/MetricsCollector.h         ${SRC}/src/metrics/MetricsCollector.cpp

    # ── 老化测试 ──
    ${SRC}/src/test/TestRunner.h        ${SRC}/src/test/TestRunner.cpp

    # ── 内置 Modbus 从站模拟器 ──
    ${SRC}/src/simulator/ModbusSimulator.h ${SRC}/src/simulator/ModbusSimulator.cpp
)

# ── 第三方源码：单独成组，不参与项目级警告 ──
# 它们的警告量很大且与本代码无关，混在一起会淹没真正需要看的信息。
set(THIRD_PARTY_SOURCES
    ${SRC}/third_party/qcustomplot.h ${SRC}/third_party/qcustomplot.cpp
)

# ── libmodbus（C 源码随项目编译）──
set(LIBMODBUS_SOURCES
    ${SRC}/third_party/libmodbus/modbus.c
    ${SRC}/third_party/libmodbus/modbus-data.c
    ${SRC}/third_party/libmodbus/modbus-tcp.c
    ${SRC}/third_party/libmodbus/modbus-rtu.c
)

qt_add_executable(EquipmentBurnInMonitor
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${LIBMODBUS_SOURCES}
)

# ── 编译警告 ──
# -Wall / -Wextra 会把"变量未使用、有符号与无符号混用、switch 漏枚举分支"这类
# 可疑写法报出来。只对项目自身源码开启，第三方代码不受影响。
# 这里不配 -Werror：警告只提示不阻断，避免日常开发被卡住。
if(NOT MSVC)
    set_source_files_properties(${PROJECT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config.json
    COPYONLY
)

target_include_directories(EquipmentBurnInMonitor PRIVATE
    ${SRC}
    ${SRC}/pages
    ${SRC}/third_party
    ${SRC}/third_party/libmodbus
    ${SRC}/src
)

target_link_libraries(EquipmentBurnInMonitor PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Sql
    Qt6::Network
    Qt6::Core
    ws2_32
)

# FD_SETSIZE 必须足够大：libmodbus 在 Windows 上会把 socket 句柄值 >= FD_SETSIZE 的连接
# 当异常拒绝（errno=EINVAL）。Qt 程序启动后句柄基数高，1024 不够，调大到 8192。
target_compile_definitions(EquipmentBurnInMonitor PRIVATE FD_SETSIZE=8192)

# ── AddressSanitizer（可选开关，默认关）──
# 开启：configure 时加 -DENABLE_ASAN=ON（Qt Creator：Projects → Build → CMake → 初始 CMake 参数）
# MinGW/GCC：-fsanitize=address 必须同时出现在编译与链接；-g 保留调试信息，报错才有函数名与行号
# 只用于排查构建：ASan 版本慢约 2 倍、内存约 3 倍，日常与演示构建保持默认关
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    if(MSVC)
        target_compile_options(EquipmentBurnInMonitor PRIVATE /fsanitize=address)
    else()
        target_compile_options(EquipmentBurnInMonitor PRIVATE
            -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(EquipmentBurnInMonitor PRIVATE -fsanitize=address)
    endif()
endif()

if(Qt6_VERSION VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.EquipmentBurnInMonitor)
endif()
set_target_properties(EquipmentBurnInMonitor PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS EquipmentBurnInMonitor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(EquipmentBurnInMonitor)
