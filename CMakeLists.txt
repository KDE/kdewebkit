cmake_minimum_required(VERSION 3.5)

set(KF_VERSION "5.80.0") # handled by release scripts
set(KF_DEP_VERSION "5.79.0") # handled by release scripts
project(KDEWebKit VERSION ${KF_VERSION})

include(FeatureSummary)
find_package(ECM 5.79.0  NO_MODULE)
set_package_properties(ECM PROPERTIES TYPE REQUIRED DESCRIPTION "Extra CMake Modules." URL "https://commits.kde.org/extra-cmake-modules")
feature_summary(WHAT REQUIRED_PACKAGES_NOT_FOUND FATAL_ON_MISSING_REQUIRED_PACKAGES)


set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

option(BUILD_DESIGNERPLUGIN "Build plugin for Qt Designer" ON)
add_feature_info(DESIGNERPLUGIN ${BUILD_DESIGNERPLUGIN} "Build plugin for Qt Designer")

set(REQUIRED_QT_VERSION 5.14.0)
find_package(Qt5 ${REQUIRED_QT_VERSION} CONFIG REQUIRED Core Widgets WebKitWidgets Network)
include(KDEInstallDirs)
include(KDEFrameworkCompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)


include(GenerateExportHeader)

include(ECMSetupVersion)
include(ECMGenerateHeaders)

ecm_setup_version(PROJECT VARIABLE_PREFIX KDEWEBKIT
                        VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kdewebkit_version.h"
                        PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/KF5WebKitConfigVersion.cmake"
                        SOVERSION 5)

find_package(KF5Config ${KF_DEP_VERSION} REQUIRED)
find_package(KF5CoreAddons ${KF_DEP_VERSION} REQUIRED)
find_package(KF5KIO ${KF_DEP_VERSION} REQUIRED)
find_package(KF5JobWidgets ${KF_DEP_VERSION} REQUIRED)
find_package(KF5Parts ${KF_DEP_VERSION} REQUIRED)
find_package(KF5Service ${KF_DEP_VERSION} REQUIRED)
find_package(KF5Wallet ${KF_DEP_VERSION} REQUIRED)

# remove definitions set by KDEFrameworkCompilerSettings which we fail to meet
remove_definitions(-DQT_NO_KEYWORDS)
remove_definitions(-DQT_NO_FOREACH)
remove_definitions(-DQT_NO_CAST_FROM_ASCII)
remove_definitions(-DQT_NO_CAST_FROM_BYTEARRAY)
# ideally we would support QT_NO_KEYWORDS, but at least we can do this
add_definitions(-DQT_NO_SIGNALS_SLOTS_KEYWORDS)
add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0x050e00)

add_subdirectory(src)

# create a Config.cmake and a ConfigVersion.cmake file and install them
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KF5WebKit")

include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/KF5WebKitConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/KF5WebKitConfig.cmake"
  INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/KF5WebKitConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/KF5WebKitConfigVersion.cmake"
  DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
  COMPONENT Devel
)

install(EXPORT KF5WebKitTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE KF5WebKitTargets.cmake NAMESPACE KF5:: )

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/kdewebkit_version.h
  DESTINATION ${KDE_INSTALL_INCLUDEDIR_KF5} COMPONENT Devel
)

feature_summary(WHAT ALL   FATAL_ON_MISSING_REQUIRED_PACKAGES)
