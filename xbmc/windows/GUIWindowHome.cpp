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

#include "ContextMenuManager.h"
#include "GUIUserMessages.h"
#include "PlayListPlayer.h"
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

#define CONTROL_HOMESHELFMOVIESRA         8000
#define CONTROL_HOMESHELFTVSHOWSRA        8001
#define CONTROL_HOMESHELFMUSICALBUMS      8002
#define CONTROL_HOMESHELFMOVIESPR         8010
#define CONTROL_HOMESHELFTVSHOWSPR        8011

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
  m_updateHS = (Audio | Video);
  if (!CServicesManager::GetInstance().HasServices())
    m_firstRun = false;

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
  // we are only interested in library changes
  if ((flag & (VideoLibrary | AudioLibrary)) == 0)
    return;

  if (data.isMember("transaction") && data["transaction"].asBoolean())
    return;

  if (message == "OnScanStarted" || message == "OnCleanStarted" || message == "OnUpdate")
    return;

  CLog::Log(LOGDEBUG, LOGANNOUNCE, "CGUIWindowHome::Announce, from {}, message {}", sender,
            message);

  bool triggerRA = false;
  if (message == "UpdateRecentlyAdded")
  {
    if (!data.isMember("uuid"))
      triggerRA = true;

    std::string serverUUID = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(
        CSettings::SETTING_GENERAL_SERVER_UUID);
    if (serverUUID == data["uuid"].asString())
      triggerRA = true;
  }
  else
    triggerRA = true;

  if (triggerRA)
  {
    int ra_flag = 0;
    if (flag & VideoLibrary)
      ra_flag |= Video;
    if (flag & AudioLibrary)
      ra_flag |= Audio;

    CGUIMessage reload(GUI_MSG_NOTIFY_ALL, GetID(), 0, GUI_MSG_SEND_HOME_UPDATE, ra_flag);
    CServiceBroker::GetGUI()->GetWindowManager().SendThreadMessage(reload, GetID());
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
    CSingleLock lock(m_critsection);
    {
      // these can alter the gui lists and cause renderer crashing
      // if gui lists are yanked out from under rendering. Needs lock.
      CSingleLock lockGfx(CServiceBroker::GetWinSystem()->GetGfxContext());

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
    CSingleLock lock(m_critsection);

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
          CSingleLock lock(m_critsection);
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
          CSingleLock lock(m_critsection);
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
        CSingleLock lock(m_critsection);
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
        CSingleLock lock(m_critsection);
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
    CServiceBroker::GetPlaylistPlayer().Play(0);
  }
  else
  {
    CFileItem item(itemPtr);
    if (item.HasVideoInfoTag() && item.GetVideoInfoTag()->GetResumePoint().IsPartWay())
    {
      std::string resumeString = VIDEO::GetResumeString(item);
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
          item.m_lStartOffset = STARTOFFSET_RESUME;
      }
    }

    CServiceBroker::GetPlaylistPlayer().Reset();
    CServiceBroker::GetPlaylistPlayer().SetCurrentPlaylist(PLAYLIST::TYPE_VIDEO);
    PLAYLIST::CPlayList& playlist =
        CServiceBroker::GetPlaylistPlayer().GetPlaylist(PLAYLIST::TYPE_VIDEO);
    playlist.Clear();
    playlist.Add(std::make_shared<CFileItem>(item));
    // play movie...
    CServiceBroker::GetPlaylistPlayer().Play(0);
  }
  return true;
}

void CGUIWindowHome::ClearHomeShelfItems()
{
  CSingleLock lock(m_critsection);

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
    CSingleLock lock(m_critsection);
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

  CServiceBroker::GetContextMenuManager().AddVisibleItems(itemPtr, choices);

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
