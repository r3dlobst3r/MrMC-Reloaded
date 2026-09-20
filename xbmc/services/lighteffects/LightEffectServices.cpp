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

#include "LightEffectServices.h"

#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"
#include "application/ApplicationPlayer.h"
#include "application/Application.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "interfaces/AnnouncementManager.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "guilib/LocalizeStrings.h"

#include "LightEffectClient.h"

using namespace ANNOUNCEMENT;

CLightEffectServices::CLightEffectServices()
: CThread("LightEffectServices")
, m_width(32)
, m_height(32)
, m_lighteffect(nullptr)
, m_turnStaticON(false)
, m_priority(128)
{
  CServiceBroker::GetAnnouncementManager()->AddAnnouncer(this);
}

CLightEffectServices::~CLightEffectServices()
{
  if (m_lighteffect)
    m_lighteffect->SetPriority(255);
  CServiceBroker::GetAnnouncementManager()->RemoveAnnouncer(this);
  if (IsRunning())
    Stop();
}

CLightEffectServices& CLightEffectServices::GetInstance()
{
  static CLightEffectServices sLightEffectServices;
  return sLightEffectServices;
}

void CLightEffectServices::Announce(AnnouncementFlag flag, const std::string& sender, const std::string& message, const CVariant& data)
{
  if (flag == Player && sender != "xbmc")
  {
    if (message != "OnStop")
    {
      m_turnStaticON = true;
    }
    else if (message != "OnPlay")
    {
      m_priority = 128;
    }
  }

  // if setting is disabled, there is no need to even check these
  if(!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON) ||
     !CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICSCREENSAVER))
    return;
  
  if (flag == GUI && sender != "xbmc")
  {
    if (message != "OnScreensaverDeactivated")
    {
      CLog::Log(LOGDEBUG, "CLightEffectServices::Announce() [OnScreensaverDeactivated]");
      m_priority = 128;
      m_turnStaticON = true;
    }
    else if (message != "OnScreensaverActivated")
    {
      CLog::Log(LOGDEBUG, "CLightEffectServices::Announce() [OnScreensaverActivated]");
      m_priority = 255;
    }
  }
}

void CLightEffectServices::Start()
{
  std::unique_lock<CCriticalSection> lock(m_critical);
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE))
  {
    if (IsRunning())
      StopThread();
    m_blingEvent.Reset();
    CThread::Create();
  }
}

void CLightEffectServices::Stop()
{
  std::unique_lock<CCriticalSection> lock(m_critical);
  if (IsRunning())
  {
    m_blingEvent.Set();
    StopThread();
  }
}

bool CLightEffectServices::IsActive()
{
  return IsRunning();
}

void CLightEffectServices::OnSettingChanged(const std::shared_ptr<const CSetting>& setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSENABLE)
  {
    // start or stop the service
    if (std::static_pointer_cast<const CSettingBool>(setting)->GetValue())
      Start();
    else
      Stop();
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSIP ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT)
  {
    // restart to pick up ip/port changes
    if (IsRunning())
    {
      Stop();
      Start();
    }
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION    ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED         ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE         ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD)
  {
    // only set if we are running, the values
    // will get picked up when started.
    if (IsRunning())
      SetOption(settingId);
  }
  else if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB ||
           settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON)
  {
    m_turnStaticON = true;
    if (settingId == CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON)
      m_turnStaticON = std::static_pointer_cast<const CSettingBool>(setting)->GetValue();
  }

  CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
}

void CLightEffectServices::Process()
{
  while (!m_bStop)
  {
    if (InitConnection())
    {
      ApplyUserSettings();
      m_lighteffect->SetScanRange(m_width, m_height);

      SetBling();

      unsigned int capture = 0;
      int curPriority = -1;
      while (!m_bStop)
      {
        if (m_priority != curPriority)
        {
          curPriority = m_priority;
          m_lighteffect->SetPriority(curPriority);
        }
        
        const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
        if (appPlayer && appPlayer->IsPlayingVideo())
        {
          if (m_priority != 128)
            continue;
          m_turnStaticON = false;
          // if starting, alloc a rendercapture and start capturing
          if (capture == 0)
          {
            capture = appPlayer->RenderCaptureAlloc();
            appPlayer->RenderCapture(capture, m_width, m_height, CAPTUREFLAG_CONTINUOUS);
          }

          std::vector<uint8_t> pixelsBuf(m_width * m_height * 4);
          if (appPlayer->RenderCaptureGetPixels(capture, 1000, pixelsBuf.data(), pixelsBuf.size()))
          {
            //read out the pixels
            unsigned char *pixels = pixelsBuf.data();
            for (int y = 0; y < m_height; ++y)
            {
              int row = m_width * y * 4;
              for (int x = 0; x < m_width; ++x)
              {
                int pixel = row + (x * 4);
                int rgb[3] = {
                  pixels[pixel + 2],
                  pixels[pixel + 1],
                  pixels[pixel]
                };
                m_lighteffect->SetPixel(rgb, x, y);
              }
            }
            m_lighteffect->SendLights(true);
          }
        }
        else
        {
          if (capture != 0)
          {
            appPlayer->RenderCaptureRelease(capture);
            capture = 0;
          }
          // set static if its enabled
          if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICON))
          {
            // only set static colour once, no point doing it over and over again
            if (m_turnStaticON)
            {
              m_turnStaticON = false;
              m_priority = 128;
              SetAllLightsToStaticRGB();
            }
          }
          // or kill the lights
          else
          {
            m_priority = 255;
          }
          usleep(50 * 1000);
        }
      }

      // have to check this in case we go
      // right from playing to death.
      if (capture != 0)
      {
        const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
        if (appPlayer)
          appPlayer->RenderCaptureRelease(capture);
        capture = 0;
      }
      m_lighteffect->SetPriority(255);
      delete m_lighteffect; m_lighteffect = nullptr;
    }
  }
}

bool CLightEffectServices::InitConnection()
{
  m_turnStaticON = true;
  m_lighteffect = new CLightEffectClient();
  
  // boblightd server IP address and port
  const char *IP = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_SERVICES_LIGHTEFFECTSIP).c_str();
  int port = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSPORT);

  // timeout is in microseconds, so 5 seconds.
  if (!m_lighteffect->Connect(IP, port, 5000000))
  {
    m_turnStaticON = false;
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info,
      g_localizeStrings.Get(41002), g_localizeStrings.Get(41003), 3000, true);

    delete m_lighteffect; m_lighteffect = nullptr;
    return false;
  }

  return true;
}

void CLightEffectServices::ApplyUserSettings()
{
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION);
  SetOption(CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD);
}

void CLightEffectServices::SetOption(std::string setting)
{
  std::unique_lock<CCriticalSection> lock(m_critical);

  std::string value;
  std::string option;
  if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSINTERPOLATION)
  {
    option = "interpolation";
    value  = StringUtils::Format("%d", CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(setting));
  }
  else
  {
    value  = StringUtils::Format("%.1f", CServiceBroker::GetSettingsComponent()->GetSettings()->GetNumber(setting));
    if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSSATURATION)
      option = "saturation";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSVALUE)
      option = "value";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSSPEED)
      option = "speed";
    else if (setting == CSettings::SETTING_SERVICES_LIGHTEFFECTSTHRESHOLD)
      option = "threshold";
  }
    
  std::string data = StringUtils::Format("%s %s", option.c_str(), value.c_str());
  if (!m_lighteffect->SetOption(data.c_str()))
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - error: for option '%s' and value '%s'",
      option.c_str(), value.c_str());
  else
  {
    CLog::Log(LOGDEBUG, "CLightEffectServices::SetOption - option '%s' and value '%s' - Done!",
      option.c_str(), value.c_str());
    // this will refresh static colours once options are changed
    m_turnStaticON = true;
  }
}

void CLightEffectServices::SetAllLightsToStaticRGB()
{
  if (!m_lighteffect)
    return;

  int rgb[3] = {
    CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICR),
    CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICG),
    CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_SERVICES_LIGHTEFFECTSSTATICB)
  };
  
  m_lighteffect->SetPriority(128);
  m_lighteffect->SendLights(rgb, true);
}

void CLightEffectServices::SetBling()
{
  if (!m_lighteffect)
    return;

  m_lighteffect->SetPriority(128);
  for (int y = 0; y < 4; ++y)
  {
    int rgb[3] = {0,0,0};
    if (y < 3)
      rgb[y] = 255;
    m_lighteffect->SendLights(rgb, true);
    m_blingEvent.Wait(std::chrono::milliseconds(1000));
    if (m_blingEvent.Signaled())
      break;
  }
}
