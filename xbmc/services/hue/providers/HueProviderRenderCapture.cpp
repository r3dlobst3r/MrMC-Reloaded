/*
 *      Copyright (C) 2018 Team MrMC
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

#include "HueProviderRenderCapture.h"

#include "ServiceBroker.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"

CHueProviderRenderCapture::CHueProviderRenderCapture()
{
  const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
  if (appPlayer)
    m_capture = appPlayer->RenderCaptureAlloc();
}

CHueProviderRenderCapture::~CHueProviderRenderCapture()
{
  if (m_capture)
    Deinitialize();
}

bool CHueProviderRenderCapture::Initialize(unsigned int width, unsigned int height)
{
  const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
  if (appPlayer)
  {
    m_width = width;
    m_height = height;
    appPlayer->RenderCapture(m_capture, width, height, CAPTUREFLAG_CONTINUOUS);
    return true;
  }
  return false;
}

bool CHueProviderRenderCapture::Deinitialize()
{
  const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
  if (appPlayer)
    appPlayer->RenderCaptureRelease(m_capture);
  m_capture = 0;
  return true;
}

bool CHueProviderRenderCapture::WaitMSec(unsigned int milliSeconds)
{
  // capture state is polled by GetStatus/GetBuffer after the caller waits
  m_pixelWait.Wait(std::chrono::milliseconds(milliSeconds));
  return true;
}

HueProvideStatus CHueProviderRenderCapture::GetStatus()
{
  if (!m_lastPixels.empty())
    return HueProvideStatus::DONE;

  return HueProvideStatus::FAILED;
}

unsigned char* CHueProviderRenderCapture::GetBuffer()
{
  const auto appPlayer = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
  if (!appPlayer)
    return nullptr;

  m_lastPixels.resize(m_width * m_height * 4);
  if (appPlayer->RenderCaptureGetPixels(m_capture, 0, m_lastPixels.data(), m_lastPixels.size()))
  {
    m_pixelWait.Set();
    return m_lastPixels.data();
  }

  return nullptr;
}
