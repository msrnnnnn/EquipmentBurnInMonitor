cmake_minimum_required(VERSION 3.16)

project(EquipmentBurnInMonitor VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "E:/Qt/6.10.0/mingw_64" CACHE PATH "Qt6 prefix")

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql PrintSupport Network)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR})

# ── C++ 源文件 ──
set(PROJECT_SOURCES
    ${SRC}/main.cpp
    ${SRC}/mainwindow.cpp
    ${SRC}/mainwindow.h
    ${SRC}/pages/homepage.h     ${SRC}/pages/homepage.cpp
    ${SRC}/pages/videopage.h    ${SRC}/pages/videopage.cpp
    ${SRC}/pages/settingspage.h ${SRC}/pages/settingspage.cpp
    ${SRC}/equipmentdata.h      ${SRC}/equipmentdata.cpp
    ${SRC}/equipmentdataprovider.h ${SRC}/equipmentdataprovider.cpp
    ${SRC}/third_party/qcustomplot.cpp ${SRC}/third_party/qcustomplot.h
    ${SRC}/src/logging/logger.h     ${SRC}/src/logging/logger.cpp
    ${SRC}/src/config/config.h      ${SRC}/src/config/config.cpp
    ${SRC}/src/config/ConfigWatcher.h ${SRC}/src/config/ConfigWatcher.cpp
    ${SRC}/src/modbus/ModbusTypes.h ${SRC}/src/modbus/ModbusTypes.cpp
    ${SRC}/src/device/DeviceDriver.h
    ${SRC}/src/data/datacache.cpp ${SRC}/src/data/datacache.h
    ${SRC}/src/data/SqliteRepository.h ${SRC}/src/data/SqliteRepository.cpp
    ${SRC}/src/metrics/MetricsCollector.cpp ${SRC}/src/metrics/MetricsCollector.h
)

# ── libmodbus C 源文件 ──
set(LIBMODBUS_SOURCES
    ${SRC}/third_party/libmodbus/modbus.c
    ${SRC}/third_party/libmodbus/modbus-data.c
    ${SRC}/third_party/libmodbus/modbus-tcp.c
    ${SRC}/third_party/libmodbus/modbus-rtu.c
)


qt_add_executable(EquipmentBurnInMonitor
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${LIBMODBUS_SOURCES}
    src/modbus/modbustcpclient.h src/modbus/modbustcpclient.cpp
    src/modbus/modbussession.h src/modbus/modbussession.cpp
    src/device/modbusdevicedriver.h src/device/modbusdevicedriver.cpp
    src/device/simpleregisterdevice.h src/device/simpleregisterdevice.cpp
    src/device/motortemperaturedevice.h src/device/motortemperaturedevice.cpp
    src/device/currentdevice.h src/device/currentdevice.cpp
    src/scheduler/PollingScheduler.h src/scheduler/PollingScheduler.cpp
    src/api/servicefacade.h src/api/servicefacade.cpp
    src/diagnostics/healthmonitor.h src/diagnostics/healthmonitor.cpp
    src/rules/Rule.h src/rules/RuleEngine.cpp src/rules/RuleEngine.h
    src/rules/thresholdrule.h src/rules/thresholdrule.cpp
    src/rules/ratechangerule.h src/rules/ratechangerule.cpp
    src/test/TestRunner.cpp src/test/TestRunner.h
    src/simulator/modbus_simulator.h src/simulator/modbus_simulator.cpp
    # 早期异步化接口草稿 a.h/a.cpp 已删除（与现行 modbustcpclient 定义同名类，
    # 仅为历史对照保留，确认不再需要）

)

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
    ws2_32
)

# FD_SETSIZE 必须足够大：libmodbus 在 Windows 上会把 socket 句柄值 >= FD_SETSIZE 的连接
# 当异常拒绝（errno=EINVAL）。Qt 程序启动后句柄基数高，1024 不够，调大到 8192。
target_compile_definitions(EquipmentBurnInMonitor PRIVATE FD_SETSIZE=8192)
target_link_libraries(EquipmentBurnInMonitor PRIVATE Qt${QT_VERSION_MAJOR}::Core)

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
