/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "utils/HomeShelfJob.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "addons/AddonManager.h"
#include "addons/addoninfo/AddonType.h"
#include "application/Application.h"
#include "filesystem/Directory.h"
#include "guilib/GUIWindow.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "music/MusicDatabase.h"
#include "music/MusicThumbLoader.h"
#include "music/tags/MusicInfoTag.h"
#include "services/ServicesManager.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "threads/CriticalSection.h"
#include "utils/StringUtils.h"
#include "video/VideoDbUrl.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"
#include "video/VideoInfoTag.h"
#include "video/VideoThumbLoader.h"

#if defined(TARGET_DARWIN_TVOS)
#include "platform/darwin/tvos/TVOSTopShelf.h"
#endif

#define NUM_ITEMS 10

CHomeShelfJob::CHomeShelfJob(int flag)
{
  m_flag = flag;
  m_HomeShelfTVRA = new CFileItemList;
  m_HomeShelfTVPR = new CFileItemList;
  m_HomeShelfMoviesRA = new CFileItemList;
  m_HomeShelfMoviesPR = new CFileItemList;
  m_HomeShelfMusicAlbums = new CFileItemList;
  m_HomeShelfMusicVideos = new CFileItemList;
  m_HomeShelfContinueWatching = new CFileItemList;

  m_compatibleSkin = false;
  std::string skinId = CServiceBroker::GetSettingsComponent()
                           ->GetSettings()
                           ->GetString(CSettings::SETTING_LOOKANDFEEL_SKIN);
  ADDON::AddonPtr addon;
  if (CServiceBroker::GetAddonMgr().GetAddon(skinId, addon, ADDON::AddonType::SKIN, ADDON::OnlyEnabled::CHOICE_YES))
  {
    if (skinId == "skin.ariana")
      m_compatibleSkin = true;
  }
}

CHomeShelfJob::~CHomeShelfJob()
{
  delete m_HomeShelfTVRA;
  delete m_HomeShelfTVPR;
  delete m_HomeShelfMoviesRA;
  delete m_HomeShelfMoviesPR;
  delete m_HomeShelfMusicAlbums;
  delete m_HomeShelfMusicVideos;
  delete m_HomeShelfContinueWatching;
}

bool CHomeShelfJob::UpdateVideo()
{
  std::unique_lock<CCriticalSection> lock(m_critsection);

  CLog::Log(LOGDEBUG, "CHomeShelfJob::UpdateVideo() - Running HomeShelf screen update");

  CFileItemList homeShelfTVRA;
  CFileItemList homeShelfTVPR;
  CFileItemList homeShelfMoviesRA;
  CFileItemList homeShelfMoviesPR;

  m_HomeShelfTVRA->ClearItems();
  m_HomeShelfTVPR->ClearItems();
  m_HomeShelfMoviesRA->ClearItems();
  m_HomeShelfMoviesPR->ClearItems();
  m_HomeShelfContinueWatching->ClearItems();

  std::string serverType = CServiceBroker::GetSettingsComponent()
                               ->GetSettings()
                               ->GetString(CSettings::SETTING_GENERAL_SERVER_TYPE);
  std::string serverUUID = CServiceBroker::GetSettingsComponent()
                               ->GetSettings()
                               ->GetString(CSettings::SETTING_GENERAL_SERVER_UUID);

  bool homeScreenWatched = CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
      CSettings::SETTING_VIDEOLIBRARY_WATCHEDHOMESHELFITEMS);
  if (!m_compatibleSkin || serverType == "mrmc" || serverType.empty())
  {
    CVideoDatabase videodatabase;
    videodatabase.Open();

    if (videodatabase.HasContent() || !m_compatibleSkin)
    {
      CVideoThumbLoader loader;
      loader.OnLoaderStart();
      if (videodatabase.HasContent(VideoDbContentType::MOVIES))
        XFILE::CDirectory::GetDirectory("library://video/inprogressmovies.xml/", homeShelfMoviesPR, "",
                                        XFILE::DIR_FLAG_DEFAULTS);
      if (videodatabase.HasContent(VideoDbContentType::TVSHOWS))
        XFILE::CDirectory::GetDirectory("library://video/inprogressepisodes.xml/", homeShelfTVPR, "",
                                        XFILE::DIR_FLAG_DEFAULTS);
      homeShelfMoviesPR.Sort(SortByLastPlayed, SortOrderDescending);
      homeShelfTVPR.Sort(SortByLastPlayed, SortOrderDescending);
      for (int i = 0; i < homeShelfMoviesPR.Size() && i < NUM_ITEMS; i++)
      {
        CFileItemPtr item = homeShelfMoviesPR.Get(i);
        item->SetProperty("ItemType", g_localizeStrings.Get(41001)); // In Progress
        if (!item->HasArt("thumb"))
          loader.LoadItem(item.get());
        m_HomeShelfMoviesPR->Add(item);
      }
      for (int i = 0; i < homeShelfTVPR.Size() && i < NUM_ITEMS; i++)
      {
        CFileItemPtr item = homeShelfTVPR.Get(i);
        std::string seasonEpisode =
            StringUtils::Format("S{:02}E{:02}", item->GetVideoInfoTag()->m_iSeason,
                                item->GetVideoInfoTag()->m_iEpisode);
        item->SetProperty("SeasonEpisode", seasonEpisode);
        item->SetProperty("ItemType", g_localizeStrings.Get(41001)); // In Progress
        if (!item->HasArt("thumb"))
          loader.LoadItem(item.get());
        if (!item->HasArt("tvshow.thumb"))
          item->SetArt("tvshow.thumb", item->GetArt("season.poster"));
        m_HomeShelfTVPR->Add(item);
      }

      std::string path = "videodb://recentlyaddedmovies/";
      if (!homeScreenWatched)
      {
        CVideoDbUrl url;
        if (url.FromString(path))
        {
          url.AddOption(
              "filter",
              "{\"type\":\"movies\", \"rules\":[{\"field\":\"playcount\", \"operator\":\"is\", \"value\":\"0\"}]}");
          path = url.ToString();
        }
      }

      videodatabase.GetRecentlyAddedMoviesNav(path, homeShelfMoviesRA, NUM_ITEMS);

      for (int i = 0; i < homeShelfMoviesRA.Size(); i++)
      {
        CFileItemPtr item = homeShelfMoviesRA.Get(i);
        item->SetProperty("ItemType", g_localizeStrings.Get(41000)); // Recently Added
        if (!item->HasArt("thumb"))
          loader.LoadItem(item.get());
        m_HomeShelfMoviesRA->Add(item);
      }

      path = "videodb://recentlyaddedepisodes/";
      if (!homeScreenWatched)
      {
        CVideoDbUrl url;
        if (url.FromString(path))
        {
          url.AddOption("filter",
                        "{\"type\":\"episodes\", \"rules\":[{\"field\":\"playcount\", \"operator\":\"is\", \"value\":\"0\"}]}");
          path = url.ToString();
        }
      }
      videodatabase.GetRecentlyAddedEpisodesNav(path, homeShelfTVRA, NUM_ITEMS);
      for (int i = 0; i < homeShelfTVRA.Size(); i++)
      {
        CFileItemPtr item = homeShelfTVRA.Get(i);
        std::string seasonEpisode =
            StringUtils::Format("S{:02}E{:02}", item->GetVideoInfoTag()->m_iSeason,
                                item->GetVideoInfoTag()->m_iEpisode);
        item->SetProperty("SeasonEpisode", seasonEpisode);
        item->SetProperty("ItemType", g_localizeStrings.Get(41000)); // Recently Added
        if (!item->HasArt("thumb"))
          loader.LoadItem(item.get());
        if (!item->HasArt("tvshow.thumb"))
          item->SetArt("tvshow.thumb", item->GetArt("season.poster"));
        m_HomeShelfTVRA->Add(item);
      }
    }
    if (!m_compatibleSkin)
    {
      // get InProgress TVSHOWS and MOVIES from any enabled service
      CServicesManager::GetInstance().GetAllInProgressShows(*m_HomeShelfTVPR, NUM_ITEMS);
      CServicesManager::GetInstance().GetAllInProgressMovies(*m_HomeShelfMoviesPR, NUM_ITEMS);
      // get recently added TVSHOWS and MOVIES from any enabled service
      CServicesManager::GetInstance().GetAllRecentlyAddedShows(*m_HomeShelfTVRA, NUM_ITEMS,
                                                               homeScreenWatched);
      CServicesManager::GetInstance().GetAllRecentlyAddedMovies(*m_HomeShelfMoviesRA, NUM_ITEMS,
                                                                homeScreenWatched);
    }
    videodatabase.Close();
  }
  else
  {
    // get recently added TVSHOWS and MOVIES for chosen server in Home Screen, get 20 items as its not as slow as it was before
    CServicesManager::GetInstance().GetRecentlyAddedShows(*m_HomeShelfTVRA, NUM_ITEMS * 2, true,
                                                          serverType, serverUUID);
    CServicesManager::GetInstance().GetRecentlyAddedMovies(*m_HomeShelfMoviesRA, NUM_ITEMS * 2,
                                                           true, serverType, serverUUID);
    CServicesManager::GetInstance().GetInProgressShows(*m_HomeShelfTVPR, NUM_ITEMS * 2, serverType,
                                                       serverUUID);
    CServicesManager::GetInstance().GetInProgressMovies(*m_HomeShelfMoviesPR, NUM_ITEMS * 2,
                                                        serverType, serverUUID);
    CServicesManager::GetInstance().GetContinueWatching(*m_HomeShelfContinueWatching, serverType,
                                                        serverUUID);
  }

  m_HomeShelfTVRA->SetContent("episodes");
  m_HomeShelfTVPR->SetContent("episodes");
  m_HomeShelfMoviesRA->SetContent("movies");
  m_HomeShelfMoviesPR->SetContent("movies");
  m_HomeShelfContinueWatching->SetContent("movies");
#if defined(TARGET_DARWIN_TVOS)
  // send recently added Movies and TvShows to TopShelf
  CTVOSTopShelf::GetInstance().SetTopShelfItems(*m_HomeShelfMoviesRA, *m_HomeShelfTVRA,
                                                *m_HomeShelfMoviesPR, *m_HomeShelfTVPR);
#endif

  return true;
}

bool CHomeShelfJob::UpdateMusic()
{
  std::unique_lock<CCriticalSection> lock(m_critsection);

  CLog::Log(LOGDEBUG, "CHomeShelfJob::UpdateMusic() - Running HomeShelf screen update");

  std::string serverType = CServiceBroker::GetSettingsComponent()
                               ->GetSettings()
                               ->GetString(CSettings::SETTING_GENERAL_SERVER_TYPE);
  std::string serverUUID = CServiceBroker::GetSettingsComponent()
                               ->GetSettings()
                               ->GetString(CSettings::SETTING_GENERAL_SERVER_UUID);

  if (!m_compatibleSkin || serverType == "mrmc" || serverType.empty())
  {
    CMusicDatabase musicdatabase;
    musicdatabase.Open();
    if (musicdatabase.GetSongsCount() > 0)
    {
      VECALBUMS albums;
      musicdatabase.GetRecentlyAddedAlbums(albums, NUM_ITEMS);
      for (size_t i = 0; i < albums.size(); ++i)
      {
        CAlbum& album = albums[i];
        std::string strDir = StringUtils::Format("musicdb://albums/{}/", album.idAlbum);
        CFileItemPtr pItem(new CFileItem(strDir, album));
        std::string strThumb = musicdatabase.GetArtForItem(album.idAlbum, MediaTypeAlbum, "thumb");
        std::string strFanart =
            musicdatabase.GetArtForItem(album.idAlbum, MediaTypeAlbum, "fanart");
        pItem->SetProperty("thumb", strThumb);
        pItem->SetProperty("fanart", strFanart);
        pItem->SetProperty("artist", album.GetAlbumArtistString());
        pItem->SetProperty("ItemType", g_localizeStrings.Get(41000)); // Recently Added
        m_HomeShelfMusicAlbums->Add(pItem);
      }
      musicdatabase.Close();
    }
    if (!serverType.empty() && serverType != "mrmc")
    {
      // get recently added ALBUMS from any enabled service
      CServicesManager::GetInstance().GetAllRecentlyAddedAlbums(*m_HomeShelfMusicAlbums, NUM_ITEMS);
    }
  }
  else
  {
    // get recently added ALBUMS for chosen server in Home Screen
    CServicesManager::GetInstance().GetRecentlyAddedAlbums(*m_HomeShelfMusicAlbums, NUM_ITEMS,
                                                           serverType, serverUUID);
  }

  return true;
}

void CHomeShelfJob::UpdateTvItemsRA(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfTVRA);
}

void CHomeShelfJob::UpdateTvItemsPR(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfTVPR);
}

void CHomeShelfJob::UpdateMovieItemsRA(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfMoviesRA);
}

void CHomeShelfJob::UpdateMovieItemsPR(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfMoviesPR);
}

void CHomeShelfJob::UpdateContinueWatchingItems(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfContinueWatching);
}

void CHomeShelfJob::UpdateMusicAlbumItems(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfMusicAlbums);
}

void CHomeShelfJob::UpdateMusicVideoItems(CFileItemList* list)
{
  std::unique_lock<CCriticalSection> lock(m_critsection);
  list->Assign(*m_HomeShelfMusicVideos);
}

bool CHomeShelfJob::DoWork()
{
  bool ret = true;

  if (g_application.IsStopping())
    return ret;

  if (m_flag & Audio)
    ret &= UpdateMusic();

  if (g_application.IsStopping())
    return ret;

  if (m_flag & Video)
    ret &= UpdateVideo();

  return ret;
}
