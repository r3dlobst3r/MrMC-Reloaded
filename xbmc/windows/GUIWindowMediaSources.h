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

#pragma once

#include "windows/GUIMediaWindow.h"

class CFileItemList;

class CGUIWindowMediaSources : public CGUIMediaWindow
{
public:
  CGUIWindowMediaSources(void);
  ~CGUIWindowMediaSources(void) override;

  static CGUIWindowMediaSources& GetInstance();

  bool OnAction(const CAction& action) override;
  bool OnBack(int actionID) override;
  bool OnMessage(CGUIMessage& message) override;

protected:
  bool VerifyLogout(const std::string& service);
  bool Update(const std::string& strDirectory, bool updateFilterPath = true) override;
  bool GetDirectory(const std::string& strDirectory, CFileItemList& items) override;
  bool OnClick(int iItem, const std::string& player = "") override;
  std::string GetStartFolder(const std::string& dir) override;

  VECSOURCES m_shares;
};
