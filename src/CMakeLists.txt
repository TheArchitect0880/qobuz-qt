target_sources(qobuz-qt PRIVATE
    # Entry point
    main.cpp

    # Main window
    mainwindow.hpp
    mainwindow.cpp

    # Queue
    playqueue.hpp
    playqueue.cpp

    # Backend (Qt wrapper around Rust FFI)
    backend/qobuzbackend.hpp
    backend/qobuzbackend.cpp

    # Views
    view/maintoolbar.hpp
    view/maintoolbar.cpp
    view/maincontent.hpp
    view/maincontent.cpp
    view/context/view.hpp
    view/context/view.cpp
    view/queuepanel.hpp
    view/queuepanel.cpp
    view/albumlistview.hpp
    view/artistlistview.hpp
    view/artistview.hpp
    view/artistview.cpp
    view/genrebrowser.hpp
    view/genrebrowser.cpp
    view/sidepanel/view.hpp
    view/sidepanel/view.cpp

    # Lists
    list/tracks.hpp
    list/tracks.cpp
    list/library.hpp
    list/library.cpp

    # Models
    model/tracklistmodel.hpp
    model/tracklistmodel.cpp

    # Dialogs
    dialog/login.hpp
    dialog/login.cpp
    dialog/settings.hpp
    dialog/settings.cpp

    # Scrobbler (header-only)
    scrobbler/lastfm.hpp

    # Widgets (header-only)
    widget/clickableslider.hpp
    widget/volumebutton.hpp

    # Utils (header-only)
    util/icon.hpp
    util/settings.hpp
)

if (USE_DBUS)
    target_sources(qobuz-qt PRIVATE
        backend/mpris.hpp
        backend/mpris.cpp
    )
endif ()
