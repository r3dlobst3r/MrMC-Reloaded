/*
 *      Copyright (C) 2016 Team MrMC
 *      https://github.com/MrMC
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
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "ServicesDirectory.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "guilib/LocalizeStrings.h"
#include "music/MusicDatabase.h"
#include "services/ServicesManager.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"

using namespace XFILE;

bool CServicesDirectory::GetDirectory(const CURL& url, CFileItemList& items)
{
  CLog::Log(LOGDEBUG, "CServicesDirectory::GetDirectory");

  items.ClearItems();
  std::string strUrl = url.Get();
  std::string section = URIUtils::GetFileName(strUrl);
  items.SetPath(strUrl);
  std::string basePath = strUrl;
  URIUtils::RemoveSlashAtEnd(basePath);
  basePath = URIUtils::GetFileName(basePath);

  CLog::Log(LOGDEBUG, "CServicesDirectory::GetDirectory strURL = {}", CURL::GetRedacted(strUrl));

  if (StringUtils::StartsWithNoCase(strUrl, "services://movies/"))
  {
    if (section.empty())
    {
      CVideoDatabase database;
      database.Open();
      bool hasMovies = database.HasContent(VideoDbContentType::MOVIES);
      database.Close();

      if (hasMovies)
      {
        // add local Movies
        std::string title = StringUtils::Format("MrMC - {}", g_localizeStrings.Get(342));
        CFileItemPtr pItem(new CFileItem(title));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedmovies")
          pItem->SetPath("videodb://recentlyaddedmovies/");
        else if (URIUtils::GetFileName(basePath) == "inprogressmovies")
          pItem->SetPath("library://video/inprogressmovies.xml/");
        else
          pItem->SetPath("videodb://movies/" + basePath + "/");
        pItem->SetLabel(title);
        items.Add(pItem);
      }
    }

    if (CServicesManager::GetInstance().HasServices())
    {
      CURL url2(url);
      // check for plex
      if (CServicesManager::GetInstance().HasPlexServices())
      {
        url2.SetProtocol("plex");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      // check for emby
      if (CServicesManager::GetInstance().HasEmbyServices())
      {
        url2.SetProtocol("emby");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      items.SetPath(url.Get());
      items.SetLabel("Libraries");
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "services://tvshows/"))
  {
    if (section.empty())
    {
      CVideoDatabase database;
      database.Open();
      bool hasTvShows = database.HasContent(VideoDbContentType::TVSHOWS);
      database.Close();

      if (hasTvShows)
      {
        // add local Shows
        std::string title = StringUtils::Format("MrMC - {}", g_localizeStrings.Get(20343));
        CFileItemPtr pItem(new CFileItem(title));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedepisodes")
          pItem->SetPath("videodb://recentlyaddedepisodes/");
        else if (URIUtils::GetFileName(basePath) == "inprogressshows")
          pItem->SetPath("videodb://inprogresstvshows/");
        else
          pItem->SetPath("videodb://tvshows/" + basePath + "/");
        pItem->SetLabel(title);
        items.Add(pItem);
      }
    }

    if (CServicesManager::GetInstance().HasServices())
    {
      CURL url2(url);
      // check for plex
      if (CServicesManager::GetInstance().HasPlexServices())
      {
        url2.SetProtocol("plex");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      // check for emby
      if (CServicesManager::GetInstance().HasEmbyServices())
      {
        url2.SetProtocol("emby");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      items.SetPath(url.Get());
      items.SetLabel("Libraries");
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "services://music/"))
  {
    if (section.empty())
    {
      CMusicDatabase database;
      database.Open();
      bool hasMusic = database.GetSongsCount() > 0;
      database.Close();

      if (hasMusic)
      {
        // add local Music
        std::string title = StringUtils::Format("MrMC - {}", g_localizeStrings.Get(249));
        CFileItemPtr pItem(new CFileItem(title));
        pItem->m_bIsFolder = true;
        pItem->m_bIsShareOrDrive = false;
        pItem->SetPath("musicdb://" + basePath + "/");
        pItem->SetLabel(title);
        items.Add(pItem);
      }
    }

    if (CServicesManager::GetInstance().HasServices())
    {
      CURL url2(url);
      // check for plex
      if (CServicesManager::GetInstance().HasPlexServices())
      {
        url2.SetProtocol("plex");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      // check for emby
      if (CServicesManager::GetInstance().HasEmbyServices())
      {
        url2.SetProtocol("emby");
        CDirectory::GetDirectory(url2, items, m_strFileMask, m_flags);
      }
      items.SetPath(url.Get());
      items.SetLabel("Libraries");
    }
    return true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "CServicesDirectory::GetDirectory got nothing from {}",
              CURL::GetRedacted(strUrl));
  }

  return false;
}

DIR_CACHE_TYPE CServicesDirectory::GetCacheType(const CURL& url) const
{
  return DIR_CACHE_NEVER;
}
