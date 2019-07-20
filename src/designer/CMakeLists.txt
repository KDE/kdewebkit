kf5designerplugin_add_plugin(kdewebkitwidgets kdewebkit.widgets)
set_target_properties(kdewebkitwidgets PROPERTIES
    OUTPUT_NAME kdewebkit5widgets
)

target_link_libraries(kdewebkitwidgets KF5::WebKit)
if(NOT WIN32)
    # Since there are no libraries provided by this module,
    # there is no point including the build tree in RPath,
    # and then having to edit it at install time.
    set_target_properties(kdewebkitwidgets PROPERTIES
        SKIP_BUILD_RPATH TRUE
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS kdewebkitwidgets DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/designer)
