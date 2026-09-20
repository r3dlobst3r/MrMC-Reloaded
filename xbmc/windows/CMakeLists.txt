set(SOURCES GUIMediaWindow.cpp
            GUIWindowDebugInfo.cpp
            GUIWindowFileManager.cpp
            GUIWindowHome.cpp
            GUIWindowLoginScreen.cpp
            GUIWindowMediaSources.cpp
            GUIWindowPointer.cpp
            GUIWindowScreensaver.cpp
            GUIWindowScreensaverDim.cpp
            GUIWindowSplash.cpp
            GUIWindowStartup.cpp
            GUIWindowSystemInfo.cpp)

set(HEADERS GUIMediaWindow.h
            GUIWindowDebugInfo.h
            GUIWindowFileManager.h
            GUIWindowHome.h
            GUIWindowLoginScreen.h
            GUIWindowMediaSources.h
            GUIWindowPointer.h
            GUIWindowScreensaver.h
            GUIWindowScreensaverDim.h
            GUIWindowSplash.h
            GUIWindowStartup.h
            GUIWindowSystemInfo.h)

core_add_library(windows)
