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

#pragma once

#include "FileItem.h"
#include "guilib/GUIWindow.h"
#include "services/emby/EmbyClient.h"
#include "services/emby/EmbyViewCache.h"
#include "services/emby/EmbyServices.h"
#include "services/emby/EmbyUtils.h"
#include "services/plex/PlexServices.h"
#include "interfaces/IAnnouncer.h"
#include "threads/CriticalSection.h"
#include "utils/Job.h"

class CVariant;

class CGUIWindowHome :
      public CGUIWindow,
      public ANNOUNCEMENT::IAnnouncer,
      public IJobCallback
{
public:
  CGUIWindowHome(void);
  ~CGUIWindowHome(void) override;
  void OnInitWindow() override;

  void Announce(ANNOUNCEMENT::AnnouncementFlag flag,
                const std::string& sender,
                const std::string& message,
                const CVariant& data) override;

  bool OnMessage(CGUIMessage& message) override;
  bool OnAction(const CAction& action) override;

  void OnJobComplete(unsigned int jobID, bool success, CJob* job) override;

private:
  int m_updateHS{0}; // flag for which home shelf items needs to be queried
  bool m_firstRun{true};
  void AddHomeShelfJobs(int flag);
  bool OnClickHomeShelfItem(const CFileItem& itemPtr, int action);
  bool PlayHomeShelfItem(const CFileItem& itemPtr);
  bool m_HomeShelfRunning{false};
  int m_cumulativeUpdateFlag{0};
  CCriticalSection m_critsection;
  CFileItemList* m_HomeShelfTVRA;
  CFileItemList* m_HomeShelfTVPR;
  CFileItemList* m_HomeShelfMoviesRA;
  CFileItemList* m_HomeShelfMoviesPR;
  CFileItemList* m_HomeShelfMusicAlbums;
  CFileItemList* m_HomeShelfContinueWatching;

  void ClearHomeShelfItems();
  void SetContextMenuItems(int iControl);
  int GetSelectedItem(int iControl);

  // dynamic home menu (dynamichome="true" skins: opacity, ariana.touch)
  void SetupServices();
  void SetupStaticHomeButtons();
  void SetupMrMCHomeButtons();
  CFileItemPtr MakeButton(const CFileItem& item);
  void AddPlexSection(CPlexClientPtr client);
  void AddEmbySection(CEmbyClientPtr client);
  bool m_triggerRA{false};
  CFileItemList* m_buttonSections;
};
