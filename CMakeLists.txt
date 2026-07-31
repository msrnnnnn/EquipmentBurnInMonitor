cmake_minimum_required(VERSION 3.16)

project(EquipmentBurnInMonitor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 显式设置 Qt6 的安装路径（确保你的路径正确）
set(CMAKE_PREFIX_PATH "E:/Qt/6.10.0/mingw_64" CACHE PATH "Qt6 prefix")

# 只查找 Qt6，不再依赖任何版本的变量
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql PrintSupport)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    pages/homepage.h     pages/homepage.cpp
    pages/videopage.h    pages/videopage.cpp
    pages/settingspage.h pages/settingspage.cpp
    equipmentdata.h      equipmentdata.cpp
    equipmentdataprovider.h equipmentdataprovider.cpp
    third_party/qcustomplot.cpp third_party/qcustomplot.h
    src/logging/logger.h     src/logging/logger.cpp
    src/config/config.h      src/config/config.cpp
    src/config/ConfigWatcher.h src/config/ConfigWatcher.cpp
    src/modbus/ModbusTypes.h src/modbus/ModbusTypes.cpp
    src/device/DeviceDriver.cpp src/device/DeviceDriver.h
    src/data/datacache.cpp src/data/datacache.h
    src/data/SqliteRepository.h src/data/SqliteRepository.cpp
)

qt_add_executable(EquipmentBurnInMonitor
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    src/metrics/MetricsCollector.cpp src/metrics/MetricsCollector.h
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config.json
    COPYONLY
)

target_include_directories(EquipmentBurnInMonitor PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pages
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(EquipmentBurnInMonitor PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Sql
)

# 修正后的版本判断（使用 Qt6_VERSION）
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
