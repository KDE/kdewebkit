include(ECMAddQtDesignerPlugin)

ecm_qtdesignerplugin_widget(KWebView
    TOOLTIP "KDE WebKit View"
    WHATSTHIS "A WebKit view with KDE integration."
    GROUP "Views (KF5 - Deprecated)"
)

ecm_add_qtdesignerplugin(kdewebkitwidgets
    NAME KDEWebKitWidgets
    OUTPUT_NAME kdewebkit5widgets
    WIDGETS
        KWebView
    LINK_LIBRARIES KF5::WebKit
    INSTALL_DESTINATION "${KDE_INSTALL_QTPLUGINDIR}/designer"
)
