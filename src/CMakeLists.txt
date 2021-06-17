add_library(KF5WebKit)
add_library(KF5::WebKit ALIAS KF5WebKit)

set_target_properties(KF5WebKit PROPERTIES
    VERSION     ${KDEWEBKIT_VERSION}
    SOVERSION   ${KDEWEBKIT_SOVERSION}
    EXPORT_NAME WebKit
)

target_sources(KF5WebKit PRIVATE
    kwebwallet.cpp
    kgraphicswebview.cpp
    kwebpage.cpp
    kwebview.cpp
    kwebpluginfactory.cpp
)

generate_export_header(KF5WebKit BASE_NAME KDEWebKit)

target_include_directories(KF5WebKit INTERFACE "$<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR_KF5}/KDEWebKit>")

target_link_libraries(KF5WebKit PUBLIC Qt5::WebKitWidgets
                                PRIVATE KF5::CoreAddons KF5::Wallet KF5::KIOWidgets KF5::Parts)

ecm_generate_headers(KDEWebKit_HEADERS
  HEADER_NAMES
  KWebWallet
  KGraphicsWebView
  KWebPluginFactory
  KWebPage
  KWebView

  REQUIRED_HEADERS KDEWebKit_HEADERS
)

install(TARGETS KF5WebKit EXPORT KF5WebKitTargets ${KF5_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/kdewebkit_export.h
  ${KDEWebKit_HEADERS}
  DESTINATION ${KDE_INSTALL_INCLUDEDIR_KF5}/KDEWebKit COMPONENT Devel
)

if(BUILD_DESIGNERPLUGIN)
    add_subdirectory(designer)
endif()

include(ECMGeneratePriFile)
ecm_generate_pri_file(BASE_NAME KDEWebKit LIB_NAME KF5WebKit DEPS "webkitwidgets" FILENAME_VAR PRI_FILENAME INCLUDE_INSTALL_DIR ${KDE_INSTALL_INCLUDEDIR_KF5}/KDEWebKit)
install(FILES ${PRI_FILENAME} DESTINATION ${ECM_MKSPECS_INSTALL_DIR})
