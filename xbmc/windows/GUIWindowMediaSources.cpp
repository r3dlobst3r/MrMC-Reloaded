/*
 *  Copyright (C) 2017 Team MrMC
 *  https://github.com/MrMC
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

#include "GUIWindowMediaSources.h"

#include "FileItem.h"
#include "ServiceBroker.h"
#include "URL.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "input/actions/ActionIDs.h"
#include "services/emby/EmbyServices.h"
#include "services/plex/PlexClient.h"
#include "services/plex/PlexServices.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/Base64URL.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include <utility>
#include <vector>

// MrMC "Would you like to sign-out of {} service?"
#define MRMC_STRING_CONFIRM_SIGNOUT 41276

CGUIWindowMediaSources::CGUIWindowMediaSources(void)
  : CGUIMediaWindow(WINDOW_MEDIA_SOURCES, "MyMediaSources.xml")
{
}

CGUIWindowMediaSources::~CGUIWindowMediaSources(void) = default;

CGUIWindowMediaSources& CGUIWindowMediaSources::GetInstance()
{
  static CGUIWindowMediaSources sWNav;
  return sWNav;
}

bool CGUIWindowMediaSources::OnAction(const CAction& action)
{
  return CGUIMediaWindow::OnAction(action);
}

bool CGUIWindowMediaSources::OnBack(int actionID)
{
  if (actionID == ACTION_NAV_BACK || actionID == ACTION_PREVIOUS_MENU)
    return CGUIMediaWindow::OnBack(ACTION_NAV_BACK);
  return CGUIMediaWindow::OnBack(actionID);
}

bool CGUIWindowMediaSources::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_RESET:
      m_vecItems->SetPath("");
      break;
    case GUI_MSG_WINDOW_DEINIT:
    case GUI_MSG_WINDOW_INIT:
    case GUI_MSG_CLICKED:
      break;
  }
  return CGUIMediaWindow::OnMessage(message);
}

bool CGUIWindowMediaSources::Update(const std::string& strDirectory, bool updateFilterPath /* = true */)
{
  return CGUIMediaWindow::Update(strDirectory, updateFilterPath);
}

bool CGUIWindowMediaSources::GetDirectory(const std::string& strDirectory, CFileItemList& items)
{
  bool result;
  items.Clear();
  items.ClearArt();
  items.ClearProperties();
  items.RemoveDiscCache(GetID());

  const auto settings = CServiceBroker::GetSettingsComponent()->GetSettings();

  if (strDirectory.empty() || strDirectory == "mediasources://")
  {
    CFileItemPtr vItem(new CFileItem("Video"));
    vItem->m_bIsFolder = true;
    vItem->m_bIsShareOrDrive = true;
    vItem->SetPath("mediasources://video/");
    vItem->SetLabel(g_localizeStrings.Get(157));
    items.Add(vItem);

    CFileItemPtr mItem(new CFileItem("Music"));
    mItem->m_bIsFolder = true;
    mItem->m_bIsShareOrDrive = true;
    mItem->SetPath("mediasources://music/");
    mItem->SetLabel(g_localizeStrings.Get(2));
    items.Add(mItem);

    CFileItemPtr pItem(new CFileItem("Pictures"));
    pItem->m_bIsFolder = true;
    pItem->m_bIsShareOrDrive = true;
    pItem->SetPath("mediasources://pictures/");
    pItem->SetLabel(g_localizeStrings.Get(1213));
    items.Add(pItem);

    CFileItemPtr plItem(new CFileItem("Playlists"));
    plItem->m_bIsFolder = true;
    plItem->m_bIsShareOrDrive = true;
    plItem->SetPath("mediasources://playlists/");
    plItem->SetLabel(g_localizeStrings.Get(136));
    plItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(plItem);

    CFileItemPtr pvItem(new CFileItem("PVRAddons"));
    pvItem->m_bIsFolder = true;
    pvItem->m_bIsShareOrDrive = false;
    pvItem->SetPath("mediasources://pvr/");
    pvItem->SetLabel(g_localizeStrings.Get(24019));
    pvItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(pvItem);

    std::string text;
    const std::string strSignIn = g_localizeStrings.Get(41008);
    const std::string strSignOut = g_localizeStrings.Get(41009);
    if (settings->GetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN) == strSignIn)
      text = StringUtils::Format("{} Plex {}", strSignIn, g_localizeStrings.Get(706));
    else
      text = StringUtils::Format("{} Plex {}", strSignOut, g_localizeStrings.Get(706));

    CFileItemPtr plexItem(new CFileItem("Plex"));
    plexItem->m_bIsFolder = true;
    plexItem->m_bIsShareOrDrive = false;
    plexItem->SetPath("mediasources://plex/");
    plexItem->SetLabel(text);
    plexItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(plexItem);

    const std::string strEmbySignIn = g_localizeStrings.Get(41052);
    const std::string strEmbySignOut = g_localizeStrings.Get(41053);
    if (settings->GetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN) == strEmbySignIn)
      text = StringUtils::Format("{} Emby {}", strEmbySignIn, g_localizeStrings.Get(706));
    else
      text = StringUtils::Format("{} Emby {}", strEmbySignOut, g_localizeStrings.Get(706));

    CFileItemPtr embyItem(new CFileItem("Emby"));
    embyItem->m_bIsFolder = true;
    embyItem->m_bIsShareOrDrive = false;
    embyItem->SetPath("mediasources://emby/");
    embyItem->SetLabel(text);
    embyItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(embyItem);

    items.SetPath("mediasources://");
    items.SetLabel("");
    result = true;
  }
  else if (strDirectory == "mediasources://playlists/")
  {
    CFileItemPtr pItem(new CFileItem("MusicPlaylist"));
    pItem->m_bIsFolder = true;
    pItem->m_bIsShareOrDrive = false;
    pItem->SetPath("mediasources://musicplaylists/");
    pItem->SetLabel(g_localizeStrings.Get(20011));
    items.Add(pItem);

    CFileItemPtr plItem(new CFileItem("VideoPlaylist"));
    plItem->m_bIsFolder = true;
    plItem->m_bIsShareOrDrive = false;
    plItem->SetPath("mediasources://videoplaylists/");
    plItem->SetLabel(g_localizeStrings.Get(20012));
    plItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(plItem);

    items.SetPath("mediasources://playlists/");
    items.SetLabel(g_localizeStrings.Get(136));
    items.SetContent("playlists");
    result = true;
  }
  else if (strDirectory == "mediasources://plexplaylists/")
  {
    CFileItemPtr pItem(new CFileItem("PlexMusicPlaylist"));
    pItem->m_bIsFolder = true;
    pItem->m_bIsShareOrDrive = false;
    pItem->SetPath("mediasources://plexmusicplaylists/");
    pItem->SetLabel(g_localizeStrings.Get(20011));
    items.Add(pItem);

    CFileItemPtr plItem(new CFileItem("PlexVideoPlaylist"));
    plItem->m_bIsFolder = true;
    plItem->m_bIsShareOrDrive = false;
    plItem->SetPath("mediasources://plexvideoplaylists/");
    plItem->SetLabel(g_localizeStrings.Get(20012));
    plItem->SetSpecialSort(SortSpecialOnBottom);
    items.Add(plItem);

    items.SetPath("mediasources://plexplaylists/");
    items.SetLabel(g_localizeStrings.Get(136));
    items.SetContent("playlists");
    result = true;
  }
  else if (strDirectory == "mediasources://plexmusicplaylists/")
  {
    const std::string uuid = settings->GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
    CPlexClientPtr plexClient = CPlexServices::GetInstance().GetClient(uuid);
    if (plexClient)
    {
      std::vector<PlexSectionsContent> playlists = plexClient->GetPlaylistContent();
      for (const auto& playlist : playlists)
      {
        if (playlist.contentType == "audio")
        {
          CFileItemPtr item(new CFileItem());
          item->m_bIsFolder = true;
          item->m_bIsShareOrDrive = false;
          item->SetLabel(playlist.title);
          item->SetLabel2(playlist.duration);
          CURL curl(plexClient->GetUrl());
          curl.SetProtocol(plexClient->GetProtocol());
          curl.SetFileName(playlist.section);
          const std::string strAction =
              "mediasources://plexmusicplaylistitems/" + Base64URL::Encode(curl.Get());
          item->SetPath(strAction);
          items.Add(item);
        }
      }
    }
    items.SetContent("playlists");
    items.SetPath("mediasources://plexmusicplaylists/");
    SetHistoryForPath("mediasources://plexplaylists/");
    items.SetLabel(g_localizeStrings.Get(20011));
    result = true;
  }
  else if (strDirectory == "mediasources://plexvideoplaylists/")
  {
    const std::string uuid = settings->GetString(CSettings::SETTING_GENERAL_SERVER_UUID);
    CPlexClientPtr plexClient = CPlexServices::GetInstance().GetClient(uuid);
    if (plexClient)
    {
      std::vector<PlexSectionsContent> playlists = plexClient->GetPlaylistContent();
      for (const auto& playlist : playlists)
      {
        if (playlist.contentType == "video")
        {
          CFileItemPtr item(new CFileItem());
          item->m_bIsFolder = true;
          item->m_bIsShareOrDrive = false;
          item->SetLabel(playlist.title);
          item->SetLabel2(playlist.duration);
          CURL curl(plexClient->GetUrl());
          curl.SetProtocol(plexClient->GetProtocol());
          curl.SetFileName(playlist.section);
          const std::string strAction =
              "mediasources://plexvideoplaylistitems/" + Base64URL::Encode(curl.Get());
          item->SetPath(strAction);
          items.Add(item);
        }
      }
    }
    items.SetContent("playlists");
    items.SetPath("mediasources://plexvideoplaylists/");
    SetHistoryForPath("mediasources://plexplaylists/");
    items.SetLabel(g_localizeStrings.Get(20012));
    result = true;
  }
  else
  {
    if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://video/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("sources://video/");
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_VIDEO_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://music/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("sources://music/");
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MUSIC_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://pictures/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("sources://pictures/");
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_PICTURES, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://videoplaylists/"))
    {
      std::vector<std::string> params;
      SetHistoryForPath("mediasources://playlists/");
      params.push_back("special://videoplaylists/");
      params.push_back("return");
      params.push_back("parent_redirect=mediasources://playlists/");
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_VIDEO_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://musicplaylists/"))
    {
      std::vector<std::string> params;
      SetHistoryForPath("mediasources://playlists/");
      params.push_back("special://musicplaylists/");
      params.push_back("return");
      params.push_back("parent_redirect=mediasources://playlists/");
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MUSIC_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://plexvideoplaylistitems/"))
    {
      std::vector<std::string> params;
      const std::string section = URIUtils::GetFileName(strDirectory);
      SetHistoryForPath("mediasources://plexplaylists/");
      params.push_back("plex://movies/videoplaylists/" + section);
      params.push_back("return");
      params.push_back("parent_redirect=mediasources://plexvideoplaylists/");
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_VIDEO_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://plexmusicplaylistitems/"))
    {
      std::vector<std::string> params;
      const std::string section = URIUtils::GetFileName(strDirectory);
      SetHistoryForPath("mediasources://plexplaylists/");
      params.push_back("plex://music/musicplaylists/" + section);
      params.push_back("return");
      params.push_back("parent_redirect=mediasources://plexmusicplaylists/");
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MUSIC_NAV, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://plex/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("mediasources://");
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      const std::string strSignOut = g_localizeStrings.Get(41009);
      if (settings->GetString(CSettings::SETTING_SERVICES_PLEXSIGNINPIN) == strSignOut &&
          !VerifyLogout("Plex"))
        return false;
      CPlexServices::GetInstance().InitiateSignIn();
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MEDIA_SOURCES, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://emby/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("mediasources://");
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      const std::string strSignOut = g_localizeStrings.Get(41053);
      if (settings->GetString(CSettings::SETTING_SERVICES_EMBYSIGNINPIN) == strSignOut &&
          !VerifyLogout("Emby"))
        return false;
      CEmbyServices::GetInstance().InitiateSignIn();
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MEDIA_SOURCES, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://pvr/"))
    {
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back("addons://default_binary_addons_source/kodi.pvrclient");
      params.push_back("return");
      params.push_back("parent_redirect=mediasources://enablepvr/");
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_ADDON_BROWSER, params);
    }
    else if (StringUtils::StartsWithNoCase(strDirectory, "mediasources://enablepvr/"))
    {
      // PVR manager starts/stops automatically in Kodi 21 when clients are enabled,
      // so just return to the media-sources root.
      std::string strParentPath;
      URIUtils::GetParentPath(strDirectory, strParentPath);
      SetHistoryForPath(strParentPath);
      std::vector<std::string> params;
      params.push_back(strParentPath);
      params.push_back("return");
      params.push_back("parent_redirect=" + strParentPath);
      CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MEDIA_SOURCES, params);
    }
    result = true;
  }
  return result;
}

bool CGUIWindowMediaSources::VerifyLogout(const std::string& service)
{
  const std::string text =
      StringUtils::Format(g_localizeStrings.Get(MRMC_STRING_CONFIRM_SIGNOUT), service);
  return CGUIDialogYesNo::ShowAndGetInput(CVariant{41009}, CVariant{text});
}

bool CGUIWindowMediaSources::OnClick(int iItem, const std::string& player)
{
  return CGUIMediaWindow::OnClick(iItem, player);
}

std::string CGUIWindowMediaSources::GetStartFolder(const std::string& dir)
{
  return CGUIMediaWindow::GetStartFolder(dir);
}
