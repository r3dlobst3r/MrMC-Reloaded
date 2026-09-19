set(SOURCES ServicesManager.cpp
            emby/EmbyUtils.cpp
            emby/EmbyClient.cpp
            emby/EmbyClientSync.cpp
            emby/EmbyViewCache.cpp
            emby/EmbyServices.cpp
            plex/PlexUtils.cpp
            plex/PlexClient.cpp
            plex/PlexClientSync.cpp
            plex/PlexServices.cpp
            trakt/TraktServices.cpp
            lighteffects/LightEffectClient.cpp
            lighteffects/LightEffectServices.cpp
            hue/HueServices.cpp
            hue/HueClient.cpp
            hue/HueUtils.cpp
            hue/providers/HueProviderRenderCapture.cpp)

set(HEADERS ServicesManager.h
            emby/EmbyUtils.h
            emby/EmbyClient.h
            emby/EmbyClientSync.h
            emby/EmbyViewCache.h
            emby/EmbyServices.h
            plex/PlexUtils.h
            plex/PlexClient.h
            plex/PlexClientSync.h
            plex/PlexServices.h
            trakt/TraktServices.h
            lighteffects/LightEffectClient.h
            lighteffects/LightEffectServices.h
            hue/HueServices.h
            hue/HueClient.h
            hue/HueUtils.h
            hue/IHueProvider.h
            hue/providers/HueProviderRenderCapture.h)

if(CORE_SYSTEM_NAME STREQUAL android)
  list(APPEND SOURCES hue/providers/HueProviderAndroidProjection.cpp)
  list(APPEND HEADERS hue/providers/HueProviderAndroidProjection.h)
endif()

core_add_library(services)
