cmake_minimum_required(VERSION 3.16)

project(EquipmentBurnInMonitor VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "E:/Qt/6.10.0/mingw_64" CACHE PATH "Qt6 prefix")

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql PrintSupport)

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
    ws2_32
)

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
