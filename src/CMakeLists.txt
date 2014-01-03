set(KDEWebKit_LIB_SRCS
    kwebwallet.cpp
    kgraphicswebview.cpp
    kwebpage.cpp
    kwebview.cpp
    kwebpluginfactory.cpp
)

add_library(KF5WebKit ${KDEWebKit_LIB_SRCS})
generate_export_header(KF5WebKit BASE_NAME KDEWebKit)
add_library(KF5::WebKit ALIAS KF5WebKit)

target_include_directories(KF5WebKit INTERFACE "$<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}/KDEWebKit>")

target_link_libraries(KF5WebKit PUBLIC Qt5::WebKitWidgets
                                PRIVATE KF5::CoreAddons KF5::Wallet KF5::KIOWidgets KF5::Parts)

set_target_properties(KF5WebKit PROPERTIES VERSION   ${KDEWEBKIT_VERSION_STRING}
                                           SOVERSION ${KDEWEBKIT_SOVERSION}
                                           EXPORT_NAME WebKit
)

ecm_generate_headers(
  KWebWallet
  KGraphicsWebView
  KWebPluginFactory
  KWebPage
  KWebView

  MODULE_NAME KDEWebKit
  REQUIRED_HEADERS KDEWebKit_HEADERS
)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/KDEWebKit DESTINATION ${INCLUDE_INSTALL_DIR} COMPONENT Devel)

install(TARGETS KF5WebKit EXPORT KF5WebKitTargets ${INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/kdewebkit_export.h
  ${KDEWebKit_HEADERS}
  DESTINATION ${INCLUDE_INSTALL_DIR}/KDEWebKit COMPONENT Devel
)

