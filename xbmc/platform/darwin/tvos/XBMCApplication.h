/*
 *  Copyright (C) 2010-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#import <UIKit/UIKit.h>

@interface XBMCApplicationDelegate : UIResponder <UIApplicationDelegate>
@end

// Owns the window and drives the XBMCController through the UIScene lifecycle.
// Required by apps built with the Xcode 27 SDK (UIKit traps at launch otherwise).
@interface XBMCSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nullable, nonatomic, strong) UIWindow* window;
@end
