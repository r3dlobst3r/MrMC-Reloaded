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

#include "GUIWindowHome.h"

#include "GUIUserMessages.h"
#include "GUIInfoManager.h"
#include "addons/AddonManager.h"
#include "addons/addoninfo/AddonType.h"
#include "addons/Skin.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogSelect.h"
#include "filesystem/MultiPathDirectory.h"
#include "filesystem/StackDirectory.h"
#include "guilib/guiinfo/GUIInfoLabels.h"
#include "interfaces/builtins/Builtins.h"
#include "media/MediaType.h"
#include "playlists/PlayList.h"
#include "profiles/ProfileManager.h"
#include "settings/MediaSourceSettings.h"
#include "utils/Base64URL.h"
#include "PlayListPlayer.h"
#include "threads/CriticalSection.h"
#include "filesystem/VideoDatabaseDirectory.h"
#include "ServiceBroker.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "dialogs/GUIDialogContextMenu.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "interfaces/AnnouncementManager.h"
#include "messaging/ApplicationMessenger.h"
#include "music/MusicDatabase.h"
#include "services/ServicesManager.h"
#include "services/trakt/TraktServices.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/HomeShelfJob.h"
#include "utils/JobManager.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "video/VideoLibraryQueue.h"
#include "video/VideoUtils.h"
#include "video/dialogs/GUIDialogVideoInfo.h"
#include "video/guilib/VideoAction.h"
#include "video/windows/GUIWindowVideoBase.h"

using namespace ANNOUNCEMENT;

// MrMC: convenience for master's CSkinInfo::GetSkinSettingBool
static bool GetSkinSettingBool(const std::string& settingId)
{
  if (!g_SkinInfo)
    return false;
  auto setting = std::static_pointer_cast<const ADDON::CSkinSettingBool>(
      g_SkinInfo->GetSkinSetting(settingId));
  return setting && setting->value;
}

#define CONTROL_HOMESHELFMOVIESRA         8000
#define CONTROL_HOMESHELFTVSHOWSRA        8001
#define CONTROL_HOMESHELFMUSICALBUMS      8002
#define CONTROL_HOMESHELFMOVIESPR         8010
#define CONTROL_HOMESHELFTVSHOWSPR        8011

#define CONTROL_HOME_LIST                 9000
#define CONTROL_SERVER_BUTTON             4000
#define CONTROL_FAVOURITES_BUTTON         4001
#define CONTROL_SETTINGS_BUTTON           4002
#define CONTROL_EXTENSIONS_BUTTON         4003
#define CONTROL_PROFILES_BUTTON           4004

CGUIWindowHome::CGUIWindowHome(void) : CGUIWindow(WINDOW_HOME, "Home.xml")
{
  m_updateHS = (Audio | Video);
  m_loadType = KEEP_IN_MEMORY;

  m_HomeShelfTVRA = new CFileItemList;
  m_HomeShelfTVPR = new CFileItemList;
  m_HomeShelfMoviesRA = new CFileItemList;
  m_HomeShelfMoviesPR = new CFileItemList;
  m_HomeShelfMusicAlbums = new CFileItemList;
  m_HomeShelfContinueWatching = new CFileItemList;
  m_buttonSections = new CFileItemList;

  CServiceBroker::GetAnnouncementManager()->AddAnnouncer(this);
}

CGUIWindowHome::~CGUIWindowHome(void)
{
  CServiceBroker::GetAnnouncementManager()->RemoveAnnouncer(this);

  delete m_HomeShelfTVRA;
  delete m_HomeShelfTVPR;
  delete m_HomeShelfMoviesRA;
  delete m_HomeShelfMoviesPR;
  delete m_HomeShelfMusicAlbums;
  delete m_HomeShelfContinueWatching;
  delete m_buttonSections;
}

bool CGUIWindowHome::OnAction(const CAction& action)
{
  static unsigned int min_hold_time = 1000;
  if (action.GetID() == ACTION_NAV_BACK && action.GetHoldTime() < min_hold_time)
  {
    const auto& components = CServiceBroker::GetAppComponents();
    const auto appPlayer = components.GetComponent<CApplicationPlayer>();
    if (appPlayer->IsPlaying() && (!appPlayer->IsRemotePlaying() || appPlayer->HasAudio()))
    {
      CGUIComponent* gui = CServiceBroker::GetGUI();
      if (gui)
        gui->GetWindowManager().SwitchToFullScreen();

      return true;
    }
  }
  return CGUIWindow::OnAction(action);
}

void CGUIWindowHome::OnInitWindow()
{
  m_triggerRA = true;
  m_updateHS = (Audio | Video);
  if (!CServicesManager::GetInstance().HasServices())
    m_firstRun = false;

  SetupServices();

  if (!m_firstRun)
    AddHomeShelfJobs(m_updateHS);
  else
    m_firstRun = false;

  CGUIWindow::OnInitWindow();
}

void CGUIWindowHome::Announce(AnnouncementFlag flag,
                              const std::string& sender,
                              const std::string& message,
                              const CVariant& data)
{
  if (flag & AnnouncementFlag::PVR && message == "HomeScreenUpdate")
  {
    if (CServiceBroker::GetGUI()->GetInfoManager().GetBool(PVR_HAS_RADIO_CHANNELS, 0) ||
        CServiceBroker::GetGUI()->GetInfoManager().GetBool(PVR_HAS_TV_CHANNELS, 0))
    {
      SetupServices();
    }
    return;
  }

  // we are only interested in library changes
  if ((flag & (VideoLibrary | AudioLibrary)) == 0)
    return;

  if (data.isMember("transaction") && data["transaction"].asBoolean())
    return;

  if (message == "OnScanStarted" || message == "OnCleanStarted" || message == "OnUpdate")
    return;

  CLog::Log(LOGDEBUG, LOGANNOUNCE, "CGUIWindowHome::Announce, from {}, message {}", sender,
            message);

  m_triggerRA = false;
  if (message == "UpdateRecentlyAdded")
  {
    if (!data.isMember("uuid"))
      m_triggerRA = true;

    std::string serverUUID = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(
        CSettings::SETTING_GENERAL_SERVER_UUID);
    if (serverUUID == data["uuid"].asString())
      m_triggerRA = true;
  }
  else
    m_triggerRA = true;

  if (m_triggerRA)
  {
    m_triggerRA = false;
    int ra_flag = 0;
    if (flag & VideoLibrary)
      ra_flag |= Video;
    if (flag & AudioLibrary)
      ra_flag |= Audio;

    CGUIMessage reload(GUI_MSG_NOTIFY_ALL, GetID(), 0, GUI_MSG_SEND_HOME_UPDATE, ra_flag);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(reload, GetID());
    SetupServices();
  }
}

void CGUIWindowHome::AddHomeShelfJobs(int flag)
{
  bool getAJob = false;

  // this block checks to see if another one is running
  // and keeps track of the flag
  {
    std::unique_lock<CCriticalSection> lockMe(*this);
    if (!m_HomeShelfRunning)
    {
      getAJob = true;

      flag |= m_cumulativeUpdateFlag; // add the flags from previous calls to AddHomeShelfJobs

      m_cumulativeUpdateFlag = 0; // now taken care of in flag.
                                  // reset this since we're going to execute a job

      // we're about to add one so set the indicator
      if (flag)
        m_HomeShelfRunning = true;
    }
    else
      // since we're going to skip a job, mark that one came in and ...
      m_cumulativeUpdateFlag |= flag; // this will be used later
  }

  if (flag && getAJob)
    CServiceBroker::GetJobManager()->AddJob(new CHomeShelfJob(flag), this);

  m_updateHS = 0;
}

void CGUIWindowHome::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  auto homeShelfJob = static_cast<CHomeShelfJob*>(job);
  int jobFlag = homeShelfJob->GetFlag();

  if (jobFlag & Video)
  {
    std::unique_lock<CCriticalSection> lock(m_critsection);
    {
      // these can alter the gui lists and cause renderer crashing
      // if gui lists are yanked out from under rendering. Needs lock.
      std::unique_lock<CCriticalSection> lockGfx(CServiceBroker::GetWinSystem()->GetGfxContext());

      homeShelfJob->UpdateTvItemsRA(m_HomeShelfTVRA);
      homeShelfJob->UpdateTvItemsPR(m_HomeShelfTVPR);
      homeShelfJob->UpdateMovieItemsRA(m_HomeShelfMoviesRA);
      homeShelfJob->UpdateMovieItemsPR(m_HomeShelfMoviesPR);
      homeShelfJob->UpdateContinueWatchingItems(m_HomeShelfContinueWatching);
    }

    CGUIMessage messageTVRA(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFTVSHOWSRA, 0, 0,
                            m_HomeShelfTVRA);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageTVRA);

    CGUIMessage messageTVPR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFTVSHOWSPR, 0, 0,
                            m_HomeShelfTVPR);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageTVPR);

    CGUIMessage messageMovieRA(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMOVIESRA, 0, 0,
                               m_HomeShelfMoviesRA);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageMovieRA);

    CGUIMessage messageMoviePR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMOVIESPR, 0, 0,
                               m_HomeShelfMoviesPR);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageMoviePR);
  }

  if (jobFlag & Audio)
  {
    std::unique_lock<CCriticalSection> lock(m_critsection);

    homeShelfJob->UpdateMusicAlbumItems(m_HomeShelfMusicAlbums);
    CGUIMessage messageAlbums(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMUSICALBUMS, 0, 0,
                              m_HomeShelfMusicAlbums);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageAlbums);
  }

  int flag = 0;
  {
    std::unique_lock<CCriticalSection> lockMe(*this);

    // the job is finished.
    // did one come in in the meantime?
    flag = m_cumulativeUpdateFlag;
    m_HomeShelfRunning = false; /// we're done.
  }

  if (flag)
    AddHomeShelfJobs(
        0 /* the flag will be set inside AddHomeShelfJobs via m_cumulativeUpdateFlag */);
}

bool CGUIWindowHome::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_SETUP_HOME_SERVICES:
      SetupServices();
      break;
    case GUI_MSG_NOTIFY_ALL:
    {
      if (message.GetParam1() == GUI_MSG_SEND_HOME_UPDATE)
      {
        int updateHS = (message.GetSenderId() == GetID()) ? message.GetParam2() : (Video | Audio);

        if (IsActive())
          AddHomeShelfJobs(updateHS);
        else
          m_updateHS |= updateHS;
      }
      else if (message.GetParam1() == GUI_MSG_UPDATE_ITEM && message.GetItem())
      {
        auto newItem = std::dynamic_pointer_cast<CFileItem>(message.GetItem());
        if (newItem && IsActive())
        {
          std::unique_lock<CCriticalSection> lock(m_critsection);
          if (newItem->HasVideoInfoTag() &&
              newItem->GetVideoInfoTag()->m_type == MediaTypeMovie)
          {
            m_HomeShelfMoviesRA->UpdateItem(newItem.get());
            m_HomeShelfMoviesPR->UpdateItem(newItem.get());
          }
          else
          {
            m_HomeShelfTVRA->UpdateItem(newItem.get());
            m_HomeShelfTVPR->UpdateItem(newItem.get());
          }
          m_HomeShelfContinueWatching->UpdateItem(newItem.get());
        }
      }
      else if (message.GetParam1() == GUI_MSG_REMOVE_ITEM && message.GetItem())
      {
        auto newItem = std::dynamic_pointer_cast<CFileItem>(message.GetItem());
        if (newItem && IsActive())
        {
          std::unique_lock<CCriticalSection> lock(m_critsection);
          if (newItem->HasVideoInfoTag() &&
              newItem->GetVideoInfoTag()->m_type == MediaTypeMovie)
          {
            m_HomeShelfMoviesPR->Remove(newItem.get());
            CGUIMessage messageMoviesPR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMOVIESPR, 0,
                                        0, m_HomeShelfMoviesPR);
            CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageMoviesPR);
          }
          else
          {
            m_HomeShelfTVPR->Remove(newItem.get());
            CGUIMessage messageTVPR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFTVSHOWSPR, 0, 0,
                                    m_HomeShelfTVPR);
            CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageTVPR);
          }
          m_HomeShelfContinueWatching->Remove(newItem.get());
        }
      }
      break;
    }
    case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      bool playAction = (message.GetParam1() == ACTION_PLAYER_PLAYPAUSE ||
                         message.GetParam1() == ACTION_PLAYER_PLAY);
      bool selectAction =
          (message.GetParam1() == ACTION_SELECT_ITEM ||
           message.GetParam1() == ACTION_MOUSE_LEFT_CLICK || playAction);
      bool contextAction = (message.GetParam1() == ACTION_CONTEXT_MENU ||
                            message.GetParam1() == ACTION_MOUSE_RIGHT_CLICK);

      CFileItemList* shelfList = nullptr;
      if (iControl == CONTROL_HOMESHELFMOVIESRA)
        shelfList = m_HomeShelfMoviesRA;
      else if (iControl == CONTROL_HOMESHELFMOVIESPR)
        shelfList = m_HomeShelfMoviesPR;
      else if (iControl == CONTROL_HOMESHELFTVSHOWSRA)
        shelfList = m_HomeShelfTVRA;
      else if (iControl == CONTROL_HOMESHELFTVSHOWSPR)
        shelfList = m_HomeShelfTVPR;

      if (contextAction && shelfList)
      {
        SetContextMenuItems(iControl);
        return true;
      }
      if (selectAction && shelfList)
      {
        int item = GetSelectedItem(iControl);
        std::unique_lock<CCriticalSection> lock(m_critsection);
        if (item >= 0 && item < shelfList->Size())
        {
          CFileItemPtr itemPtr = shelfList->Get(item);
          int clickAction =
              playAction ? static_cast<int>(VIDEO::GUILIB::ACTION_PLAY_FROM_BEGINNING)
                         : static_cast<int>(VIDEO::GUILIB::ACTION_PLAY_OR_RESUME);
          OnClickHomeShelfItem(*itemPtr, clickAction);
        }
        return true;
      }
      if (selectAction && iControl == CONTROL_HOMESHELFMUSICALBUMS)
      {
        int item = GetSelectedItem(iControl);
        std::unique_lock<CCriticalSection> lock(m_critsection);
        if (item >= 0 && item < m_HomeShelfMusicAlbums->Size())
        {
          CFileItemPtr itemPtr = m_HomeShelfMusicAlbums->Get(item);
          OnClickHomeShelfItem(*itemPtr, static_cast<int>(VIDEO::GUILIB::ACTION_PLAY_FROM_BEGINNING));
        }
        return true;
      }
      break;
    }
    default:
      break;
  }

  return CGUIWindow::OnMessage(message);
}

bool CGUIWindowHome::OnClickHomeShelfItem(const CFileItem& itemPtr, int action)
{
  switch (action)
  {
    case VIDEO::GUILIB::ACTION_INFO:
      CGUIDialogVideoInfo::ShowFor(itemPtr);
      return true;
    case VIDEO::GUILIB::ACTION_PLAY_OR_RESUME:
    case VIDEO::GUILIB::ACTION_PLAY_FROM_BEGINNING:
    default:
    {
      if (itemPtr.IsAudio())
        PlayHomeShelfItem(itemPtr);
      else if (itemPtr.IsVideo())
      {
        if (itemPtr.HasVideoInfoTag() &&
            (itemPtr.GetVideoInfoTag()->m_type == MediaTypeEpisode ||
             itemPtr.GetVideoInfoTag()->m_type == MediaTypeMovie))
          PlayHomeShelfItem(itemPtr);
        else
        {
          std::vector<std::string> params;
          params.push_back(itemPtr.GetPath());
          params.push_back("return");
          CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_VIDEO_NAV, params);
        }
      }
      else
        PlayHomeShelfItem(itemPtr);
      break;
    }
  }
  return true;
}

bool CGUIWindowHome::PlayHomeShelfItem(const CFileItem& itemPtr)
{
  // play media
  if (itemPtr.IsAudio())
  {
    CFileItemList items;

    // if we are Service based, get it from there... if not, check music database
    if (itemPtr.IsMediaServiceBased())
      CServicesManager::GetInstance().GetAlbumSongs(itemPtr, items);
    else
    {
      CMusicDatabase musicdatabase;
      if (!musicdatabase.Open())
        return false;
      musicdatabase.GetItems(itemPtr.GetPath(), items);
      musicdatabase.Close();
    }
    CServiceBroker::GetPlaylistPlayer().Reset();
    CServiceBroker::GetPlaylistPlayer().SetCurrentPlaylist(PLAYLIST::TYPE_MUSIC);
    PLAYLIST::CPlayList& playlist =
        CServiceBroker::GetPlaylistPlayer().GetPlaylist(PLAYLIST::TYPE_MUSIC);
    playlist.Clear();
    playlist.Add(items);
    // play full album, starting with first song...
    CServiceBroker::GetPlaylistPlayer().Play(0, "");
  }
  else
  {
    CFileItem item(itemPtr);
    if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->GetResumePoint().IsPartWay())
    {
      std::string resumeString = VIDEO_UTILS::GetResumeString(item);
      if (!resumeString.empty())
      {
        CContextButtons choices;
        choices.Add(static_cast<int>(VIDEO::GUILIB::ACTION_RESUME), resumeString);
        choices.Add(static_cast<int>(VIDEO::GUILIB::ACTION_PLAY_FROM_BEGINNING),
                    12021); // Start from beginning
        int value = CGUIDialogContextMenu::ShowAndGetChoice(choices);
        if (value < 0)
          return false;
        if (value == VIDEO::GUILIB::ACTION_RESUME)
          item.SetStartOffset(STARTOFFSET_RESUME);
      }
    }

    CServiceBroker::GetPlaylistPlayer().Reset();
    CServiceBroker::GetPlaylistPlayer().SetCurrentPlaylist(PLAYLIST::TYPE_VIDEO);
    PLAYLIST::CPlayList& playlist =
        CServiceBroker::GetPlaylistPlayer().GetPlaylist(PLAYLIST::TYPE_VIDEO);
    playlist.Clear();
    playlist.Add(std::make_shared<CFileItem>(item));
    // play movie...
    CServiceBroker::GetPlaylistPlayer().Play(0, "");
  }
  return true;
}

void CGUIWindowHome::ClearHomeShelfItems()
{
  std::unique_lock<CCriticalSection> lock(m_critsection);

  CFileItemList* tempClearItems = new CFileItemList;
  CGUIMessage messageTVRA(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFTVSHOWSRA, 0, 0,
                          tempClearItems);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageTVRA);

  CGUIMessage messageTVPR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFTVSHOWSPR, 0, 0,
                          tempClearItems);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageTVPR);

  CGUIMessage messageMovieRA(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMOVIESRA, 0, 0,
                             tempClearItems);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageMovieRA);

  CGUIMessage messageMoviePR(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMOVIESPR, 0, 0,
                             tempClearItems);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageMoviePR);

  CGUIMessage messageAlbums(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOMESHELFMUSICALBUMS, 0, 0,
                            tempClearItems);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(messageAlbums);
}

void CGUIWindowHome::SetContextMenuItems(int iControl)
{
  CFileItemPtr itemPtr;
  CFileItemList* shelfList = nullptr;
  {
    std::unique_lock<CCriticalSection> lock(m_critsection);
    if (iControl == CONTROL_HOMESHELFMOVIESRA)
      shelfList = m_HomeShelfMoviesRA;
    else if (iControl == CONTROL_HOMESHELFMOVIESPR)
      shelfList = m_HomeShelfMoviesPR;
    else if (iControl == CONTROL_HOMESHELFTVSHOWSRA)
      shelfList = m_HomeShelfTVRA;
    else if (iControl == CONTROL_HOMESHELFTVSHOWSPR)
      shelfList = m_HomeShelfTVPR;

    if (shelfList)
    {
      int item = GetSelectedItem(iControl);
      if (item >= 0 && item < shelfList->Size())
        itemPtr = shelfList->Get(item);
    }
  }

  if (!itemPtr)
    return;
  // highlight the item
  itemPtr->Select(true);

  CContextButtons choices;

  if (itemPtr->HasVideoInfoTag() &&
      (itemPtr->GetVideoInfoTag()->m_type == MediaTypeEpisode ||
       itemPtr->GetVideoInfoTag()->m_type == MediaTypeMovie))
    choices.Add(1, 208); // play

  // Info
  if (itemPtr->HasVideoInfoTag())
  {
    if (itemPtr->GetVideoInfoTag()->m_type == MediaTypeTvShow)
      choices.Add(2, 20351);
    else if (itemPtr->GetVideoInfoTag()->m_type == MediaTypeEpisode)
      choices.Add(2, 20352);
    else if (itemPtr->GetVideoInfoTag()->m_type == MediaTypeMovie)
      choices.Add(2, 13346);
  }

  if (itemPtr->HasVideoInfoTag() && itemPtr->GetVideoInfoTag()->GetPlayCount() > 0)
    choices.Add(3, 16104); // Mark as UnWatched
  else
    choices.Add(4, 16103); // Mark as Watched

  int button = CGUIDialogContextMenu::ShowAndGetChoice(choices);

  // unhighlight the item
  itemPtr->Select(false);

  if (button == 1)
    PlayHomeShelfItem(*itemPtr);
  else if (button == 2)
    CGUIDialogVideoInfo::ShowFor(*itemPtr);
  else if (button == 3)
  {
    if (itemPtr->IsMediaServiceBased())
      CServicesManager::GetInstance().SetItemUnWatched(*itemPtr);
    else
      CVideoLibraryQueue::GetInstance().MarkAsWatched(itemPtr, false);

    if (itemPtr->HasVideoInfoTag())
      itemPtr->GetVideoInfoTag()->SetPlayCount(0);
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, itemPtr);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
    CTraktServices::GetInstance().SetItemUnWatched(*itemPtr);
  }
  else if (button == 4)
  {
    if (itemPtr->IsMediaServiceBased())
      CServicesManager::GetInstance().SetItemWatched(*itemPtr);
    else
      CVideoLibraryQueue::GetInstance().MarkAsWatched(itemPtr, true);

    if (itemPtr->HasVideoInfoTag())
      itemPtr->GetVideoInfoTag()->SetPlayCount(1);

    int guiMsg = (iControl == CONTROL_HOMESHELFMOVIESRA || iControl == CONTROL_HOMESHELFTVSHOWSRA)
                     ? GUI_MSG_UPDATE_ITEM
                     : GUI_MSG_REMOVE_ITEM;

    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, guiMsg, 0, itemPtr);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg);
    CTraktServices::GetInstance().SetItemWatched(*itemPtr);
  }
}

int CGUIWindowHome::GetSelectedItem(int iControl)
{
  CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iControl);
  OnMessage(msg);
  return msg.GetParam1();
}

// ---------------------------------------------------------------------------
// dynamic home menu for dynamichome="true" skins (skin.opacity,
// skin.ariana.touch): fills control 9000 with server/local sections and
// manages the server + profiles buttons.
// ---------------------------------------------------------------------------

void CGUIWindowHome::SetupServices()
{
  if (!g_SkinInfo || !g_SkinInfo->IsDynamicHomeCompatible())
    return;
  if (!IsActive())
    return;
  // we cannot be diddling gui unless we are on main thread.
  if (!CServiceBroker::GetAppMessenger()->IsProcessThread())
  {
    CGUIMessage msg(GUI_MSG_NOTIFY_ALL, GetID(), 0, GUI_MSG_SETUP_HOME_SERVICES);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(msg, GetID());
    return;
  }

  // Always show server button as we might lose the server and "not have services"
  SET_CONTROL_VISIBLE(CONTROL_SERVER_BUTTON);

  std::unique_lock<CCriticalSection> lock(m_critsection);
  m_buttonSections->ClearItems();
  std::string serverType = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(
      CSettings::SETTING_GENERAL_SERVER_TYPE);
  std::string serverUUID = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(
      CSettings::SETTING_GENERAL_SERVER_UUID);

  CLog::Log(LOGDEBUG, "CGUIWindowHome::SetupServices() - serverType({}) , serverUUID({})",
            serverType, serverUUID);
  if (serverType == "plex")
  {
    if (CPlexServices::GetInstance().HasClients())
    {
      CPlexClientPtr plexClient = CPlexServices::GetInstance().GetClient(serverUUID);
      if (serverUUID.empty() && !plexClient)
      {
        // first sign-in, no server selected yet - pick the first one
        plexClient = CPlexServices::GetInstance().GetFirstClient();
        if (plexClient)
        {
          CServiceBroker::GetSettingsComponent()->GetSettings()->SetString(
              CSettings::SETTING_GENERAL_SERVER_UUID, plexClient->GetUuid());
          CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
          CVariant data(CVariant::VariantTypeObject);
          data["uuid"] = plexClient->GetUuid();
          CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::VideoLibrary, "xbmc",
                                                             "UpdateRecentlyAdded", data);
          CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::AudioLibrary, "xbmc",
                                                             "UpdateRecentlyAdded", data);
        }
      }
      if (plexClient)
      {
        AddPlexSection(plexClient);
        SET_CONTROL_LABEL_THREAD_SAFE(CONTROL_SERVER_BUTTON, plexClient->GetServerName());
        std::string strLabel = CPlexServices::GetInstance().GetHomeUserName();
        std::string strThumb = CPlexServices::GetInstance().GetHomeUserThumb();
        SET_CONTROL_LABEL_THREAD_SAFE(CONTROL_PROFILES_BUTTON, strLabel);
        SET_CONTROL_LABEL2(CONTROL_PROFILES_BUTTON, strThumb);
      }
      SET_CONTROL_VISIBLE(CONTROL_PROFILES_BUTTON);
    }
  }
  else if (serverType == "emby")
  {
    if (CEmbyServices::GetInstance().HasClients())
    {
      CEmbyClientPtr embyClient = CEmbyServices::GetInstance().GetClient(serverUUID);
      if (serverUUID.empty() && !embyClient)
      {
        embyClient = CEmbyServices::GetInstance().GetFirstClient();
        if (embyClient)
        {
          CServiceBroker::GetSettingsComponent()->GetSettings()->SetString(
              CSettings::SETTING_GENERAL_SERVER_UUID, embyClient->GetUuid());
          CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
          CVariant data(CVariant::VariantTypeObject);
          data["uuid"] = embyClient->GetUuid();
          CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::VideoLibrary, "xbmc",
                                                             "UpdateRecentlyAdded", data);
          CServiceBroker::GetAnnouncementManager()->Announce(ANNOUNCEMENT::AudioLibrary, "xbmc",
                                                             "UpdateRecentlyAdded", data);
        }
      }
      if (embyClient)
      {
        AddEmbySection(embyClient);
        SET_CONTROL_LABEL_THREAD_SAFE(CONTROL_SERVER_BUTTON, embyClient->GetServerName());
      }
      SET_CONTROL_VISIBLE(CONTROL_PROFILES_BUTTON);
    }
  }
  else if (serverType == "mrmc" || serverType.empty())
  {
    SetupMrMCHomeButtons();
  }

  SetupStaticHomeButtons();
  CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), CONTROL_HOME_LIST);
  OnMessage(msg);
  int item = msg.GetParam1();

  CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_HOME_LIST, item, 0, m_buttonSections);
  CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(message);
}

void CGUIWindowHome::SetupStaticHomeButtons()
{
  bool hasLiveTv =
      (CServiceBroker::GetGUI()->GetInfoManager().GetBool(PVR_HAS_TV_CHANNELS, 0));
  bool hasRadio =
      (CServiceBroker::GetGUI()->GetInfoManager().GetBool(PVR_HAS_RADIO_CHANNELS, 0));

  bool showFavourites = (!GetControl(CONTROL_FAVOURITES_BUTTON) &&
                         !GetSkinSettingBool("HomeMenuNoFavButton"));
  bool showExtensions =
      (!GetControl(CONTROL_EXTENSIONS_BUTTON) &&
       CServiceBroker::GetAddonMgr().HasAddons(ADDON::AddonType::PLUGIN) &&
       !GetSkinSettingBool("HomeMenuNoAddonsButton"));
  bool showMediaSources = !GetSkinSettingBool("HomeMenuNoMediaSourceButton");

  // LiveTV Button
  if (hasLiveTv)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(19020)));
    pItem->SetPath("ActivateWindow(TVChannels)");
    pItem->SetProperty("type", "livetv");
    pItem->SetProperty("menu_id", "$NUMBER[12000]");
    pItem->SetProperty("id", "livetv");
    pItem->SetProperty("submenu", true);
    m_buttonSections->Add(pItem);
  }

  // Radio Button
  if (hasRadio)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(19021)));
    pItem->SetPath("ActivateWindow(RadioChannels)");
    pItem->SetProperty("type", "radio");
    pItem->SetProperty("menu_id", "$NUMBER[13000]");
    pItem->SetProperty("id", "radio");
    pItem->SetProperty("submenu", true);
    m_buttonSections->Add(pItem);
  }

  // Favourites Button
  if (showFavourites)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(10134)));
    pItem->SetPath("ActivateWindow(favourites)");
    pItem->SetProperty("type", "favorites");
    pItem->SetProperty("menu_id", "$NUMBER[14000]");
    pItem->SetProperty("id", "favorites");
    pItem->SetProperty("submenu", false);
    m_buttonSections->Add(pItem);
  }

  // MediaSources Button
  if (showMediaSources)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(20094)));
    pItem->SetPath("ActivateWindow(MediaSources,mediasources://,return)");
    pItem->SetProperty("type", "sources");
    pItem->SetProperty("menu_id", "$NUMBER[11000]");
    pItem->SetProperty("id", "video");
    pItem->SetProperty("submenu", false);
    m_buttonSections->Add(pItem);
  }

  VECSOURCES* videoSources = CMediaSourceSettings::GetInstance().GetSources("video");
  VECSOURCES* musicSources = CMediaSourceSettings::GetInstance().GetSources("music");
  for (const auto& source : *videoSources)
  {
    CFileItemPtr pItem(new CFileItem(source.strName));
    pItem->SetPath("ActivateWindow(Videos," + source.strPath + ",return)");
    pItem->SetProperty("type", "source");
    pItem->SetProperty("menu_id", "$NUMBER[19500]");
    pItem->SetProperty("id", "source");
    pItem->SetProperty("submenu", false);
    m_buttonSections->Add(pItem);
  }
  for (const auto& source : *musicSources)
  {
    CFileItemPtr pItem(new CFileItem(source.strName));
    pItem->SetPath("ActivateWindow(Music," + source.strPath + ",return)");
    pItem->SetProperty("type", "source");
    pItem->SetProperty("menu_id", "$NUMBER[19500]");
    pItem->SetProperty("id", "source");
    pItem->SetProperty("submenu", false);
    m_buttonSections->Add(pItem);
  }

  // Extensions Button
  if (showExtensions)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(24001)));
    pItem->SetPath("ActivateWindow(Programs,Addons,return)");
    pItem->SetProperty("type", "extensions");
    pItem->SetProperty("menu_id", "$NUMBER[19000]");
    pItem->SetProperty("id", "addons");
    pItem->SetProperty("submenu", false);
    m_buttonSections->Add(pItem);
  }

  // Settings Button
  if (!GetControl(CONTROL_SETTINGS_BUTTON))
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(10004)));
    pItem->SetPath("ActivateWindow(settings)");
    pItem->SetProperty("type", "system");
    pItem->SetProperty("menu_id", "$NUMBER[14000]");
    pItem->SetProperty("id", "system");
    pItem->SetProperty("submenu", true);
    m_buttonSections->Add(pItem);
  }
}

void CGUIWindowHome::SetupMrMCHomeButtons()
{
  auto& infoMgr = CServiceBroker::GetGUI()->GetInfoManager();
  bool hasPictures = (infoMgr.GetBool(LIBRARY_HAS_PICTURES, 0) &&
                      !GetSkinSettingBool("HomeMenuNoPicturesButton"));
  bool hasMusic = (infoMgr.GetBool(LIBRARY_HAS_MUSIC, 0) &&
                   !GetSkinSettingBool("HomeMenuNoMusicButton"));
  bool hasMusicVideos = (infoMgr.GetBool(LIBRARY_HAS_MUSICVIDEOS, 0) &&
                         !GetSkinSettingBool("HomeMenuNoMusicVideoButton"));

  if (CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetNumberOfProfiles() > 1)
  {
    SET_CONTROL_VISIBLE(CONTROL_PROFILES_BUTTON);
    std::string strLabel =
        CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetCurrentProfile().getName();
    std::string thumb =
        CServiceBroker::GetSettingsComponent()->GetProfileManager()->GetCurrentProfile().getThumb();
    if (thumb.empty())
      thumb = "unknown-user.png";
    SET_CONTROL_LABEL_THREAD_SAFE(CONTROL_PROFILES_BUTTON, strLabel);
    SET_CONTROL_LABEL2(CONTROL_PROFILES_BUTTON, thumb);
  }
  else
    SET_CONTROL_HIDDEN(CONTROL_PROFILES_BUTTON);

  SET_CONTROL_VISIBLE(CONTROL_SERVER_BUTTON);
  SET_CONTROL_LABEL_THREAD_SAFE(CONTROL_SERVER_BUTTON, "MrMC");

  bool hasMovies = (infoMgr.GetBool(LIBRARY_HAS_MOVIES, 0) &&
                    !GetSkinSettingBool("HomeMenuNoMovieButton"));
  bool hasTvShows = (infoMgr.GetBool(LIBRARY_HAS_TVSHOWS, 0) &&
                     !GetSkinSettingBool("HomeMenuNoTVShowButton"));
  bool flatten =
      CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_MYVIDEOS_FLATTEN);

  if (hasMovies)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(342)));
    pItem->SetPath(flatten ? "ActivateWindow(Videos,MovieTitlesLocal,return)"
                           : "ActivateWindow(Videos,movies,return)");
    pItem->SetProperty("type", "movies");
    pItem->SetProperty("menu_id", "$NUMBER[5000]");
    pItem->SetProperty("id", "movies");
    pItem->SetProperty("submenu", flatten);
    m_buttonSections->Add(pItem);
  }
  if (hasTvShows)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(20343)));
    pItem->SetPath(flatten ? "ActivateWindow(Videos,TVShowTitlesLocal,return)"
                           : "ActivateWindow(Videos,tvshows,return)");
    pItem->SetProperty("type", "tvshows");
    pItem->SetProperty("menu_id", "$NUMBER[6000]");
    pItem->SetProperty("id", "tvshows");
    pItem->SetProperty("submenu", flatten);
    m_buttonSections->Add(pItem);
  }
  if (hasMusic)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(2)));
    pItem->SetPath("ActivateWindow(Music,rootLocal,return)");
    pItem->SetProperty("type", "music");
    pItem->SetProperty("menu_id", "$NUMBER[7000]");
    pItem->SetProperty("id", "music");
    pItem->SetProperty("submenu", true);
    m_buttonSections->Add(pItem);
  }
  if (hasMusicVideos)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(20389)));
    pItem->SetPath("ActivateWindow(Videos,musicvideos,return)");
    pItem->SetProperty("type", "videos");
    pItem->SetProperty("menu_id", "$NUMBER[16000]");
    pItem->SetProperty("id", "musicvideos");
    m_buttonSections->Add(pItem);
  }
  if (hasPictures)
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(1)));
    pItem->SetPath("ActivateWindow(Pictures)");
    pItem->SetProperty("type", "pictures");
    pItem->SetProperty("menu_id", "$NUMBER[4000]");
    pItem->SetProperty("id", "pictures");
    m_buttonSections->Add(pItem);
  }
}

void CGUIWindowHome::AddPlexSection(CPlexClientPtr client)
{
  std::vector<PlexSectionsContent> contents = client->GetHomeContent();
  std::reverse(contents.begin(), contents.end());
  std::vector<PlexSectionsContent> playlists = client->GetPlaylistContent();

  if (!playlists.empty())
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(136)));
    pItem->SetLabel2("Plex-" + client->GetServerName());
    CURL curl(client->GetUrl());
    curl.SetProtocol(client->GetProtocol());
    curl.SetFileName("mediasources://plexplaylists/");
    pItem->SetPath("ActivateWindow(MediaSources,mediasources://plexplaylists/,return)");
    pItem->SetProperty("service", true);
    pItem->SetProperty("servicetype", "plex");
    pItem->SetProperty("id", "playlists");
    pItem->SetProperty("type", "playlists");
    pItem->SetProperty("base64url", Base64URL::Encode(curl.Get()));
    pItem->SetProperty("submenu",
                       CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                           CSettings::SETTING_MYVIDEOS_FLATTEN));
    m_buttonSections->AddFront(pItem, 0);
  }
  for (const auto& content : contents)
  {
    CFileItemPtr pItem(new CFileItem(content.title));
    pItem->SetLabel2("Plex-" + client->GetServerName());
    CURL curl(client->GetUrl());
    curl.SetProtocol(client->GetProtocol());

    pItem->SetProperty("service", true);
    pItem->SetProperty("servicetype", "plex");
    std::string strAction;
    std::string filename;
    std::string videoFilters;
    std::string musicFilters;
    std::string videoSufix = "all";
    std::string musicSufix = "all";
    if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
            CSettings::SETTING_MYVIDEOS_FLATTEN))
    {
      videoFilters = "filters/";
      videoSufix = "";
    }
    if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
            CSettings::SETTING_MYVIDEOS_FLATTEN))
    {
      musicFilters = "filters/";
      musicSufix = "";
    }
    if (content.type == "movie")
    {
      filename = StringUtils::Format("{}/{}", content.section, videoSufix);
      curl.SetFileName(filename);
      strAction = "plex://movies/titles/" + videoFilters + Base64URL::Encode(curl.Get());
      pItem->SetPath("ActivateWindow(Videos," + strAction + ",return)");
      pItem->SetProperty("type", "Movies");
      pItem->SetProperty("submenu",
                         CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                             CSettings::SETTING_MYVIDEOS_FLATTEN));
    }
    else if (content.type == "show")
    {
      filename = StringUtils::Format("{}/{}", content.section, videoSufix);
      curl.SetFileName(filename);
      strAction = "plex://tvshows/titles/" + videoFilters + Base64URL::Encode(curl.Get());
      pItem->SetPath("ActivateWindow(Videos," + strAction + ",return)");
      pItem->SetProperty("type", "TvShows");
      pItem->SetProperty("submenu",
                         CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                             CSettings::SETTING_MYVIDEOS_FLATTEN));
    }
    else if (content.type == "artist")
    {
      filename = StringUtils::Format("{}/{}", content.section, musicSufix);
      curl.SetFileName(filename);
      strAction = "plex://music/root/" + musicFilters + Base64URL::Encode(curl.Get());
      pItem->SetPath("ActivateWindow(Music," + strAction + ",return)");
      pItem->SetProperty("type", "Music");
      pItem->SetProperty("submenu",
                         !CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                             CSettings::SETTING_MYVIDEOS_FLATTEN));
    }
    else
      continue;
    pItem->SetProperty("base64url", Base64URL::Encode(curl.Get()));
    m_buttonSections->AddFront(pItem, 0);
  }
}

void CGUIWindowHome::AddEmbySection(CEmbyClientPtr client)
{
  std::vector<EmbyViewInfo> contents = client->GetEmbySections();
  for (const auto& content : contents)
  {
    CFileItemPtr pItem(new CFileItem(content.name));
    pItem->SetLabel2("Emby-" + client->GetServerName());
    CURL curl(client->GetUrl());
    curl.SetProtocol(client->GetProtocol());
    curl.SetFileName(content.prefix);
    pItem->SetProperty("service", true);
    pItem->SetProperty("servicetype", "emby");
    pItem->SetProperty("base64url", Base64URL::Encode(curl.Get()));
    std::string strAction;
    std::string videoFilters;
    std::string musicFilters;
    if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
            CSettings::SETTING_MYVIDEOS_FLATTEN))
      videoFilters = "filters/";
    if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
            CSettings::SETTING_MYVIDEOS_FLATTEN))
      musicFilters = "filters/";
    if (content.mediaType == "movies")
    {
      strAction = "emby://movies/titles/" + videoFilters + Base64URL::Encode(curl.Get());
      pItem->SetPath("ActivateWindow(Videos," + strAction + ",return)");
      pItem->SetProperty("type", "Movies");
      pItem->SetProperty("submenu",
                         CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                             CSettings::SETTING_MYVIDEOS_FLATTEN));
    }
    else if (content.mediaType == "tvshows")
    {
      strAction = "emby://tvshows/titles/" + videoFilters + Base64URL::Encode(curl.Get());
      pItem->SetPath("ActivateWindow(Videos," + strAction + ",return)");
      pItem->SetProperty("type", "TvShows");
      pItem->SetProperty("submenu",
                         CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
                             CSettings::SETTING_MYVIDEOS_FLATTEN));
    }
    else if (content.mediaType == "music")
    {
      strAction = "emby://music/root/" + Base64URL::Encode(curl.Get());
      pItem->SetProperty("type", "Music");
      pItem->SetPath("ActivateWindow(Music," + strAction + ",return)");
      pItem->SetProperty("submenu", false);
    }
    else
      continue;
    m_buttonSections->AddFront(pItem, 0);
  }
}
