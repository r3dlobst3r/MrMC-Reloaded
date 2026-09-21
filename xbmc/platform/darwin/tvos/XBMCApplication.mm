/*
 *  Copyright (C) 2010-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#import "platform/darwin/tvos/XBMCApplication.h"

#import "platform/darwin/NSLogDebugHelpers.h"
#import "platform/darwin/tvos/PreflightHandler.h"
#import "platform/darwin/tvos/TVOSTopShelf.h"
#import "platform/darwin/tvos/XBMCController.h"

#import <AVFoundation/AVFoundation.h>

// The controller is owned by the scene's window, but it must outlive the scene for
// applicationWillTerminate, so the app delegate keeps its own strong reference.
@interface XBMCApplicationDelegate ()
@property(nullable, nonatomic, strong) XBMCController* xbmcController;
@end

static void HandleTopShelfURL(NSURL* url)
{
  NSArray* urlComponents = [url.absoluteString componentsSeparatedByString:@"/"];
  if (urlComponents.count < 3)
    return;
  NSString* action = urlComponents[2];
  if ([action isEqualToString:@"display"] || [action isEqualToString:@"play"])
    CTVOSTopShelf::GetInstance().HandleTopShelfUrl(url.absoluteString.UTF8String, true);
}

@implementation XBMCApplicationDelegate

#pragma mark - Startup Procedures

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
  // check if apple removed our Cache folder first
  // this will trigger the restore if there is a backup available
  CPreflightHandler::CheckForRemovedCacheFolder();

  // This needs to run before anything does any CLog::Log calls
  // as they will directly cause guisetting to get accessed/created
  // via debug log settings.
  CPreflightHandler::MigrateUserdataXMLToNSUserDefaults();

  // audio session setup (app-wide, does not depend on the window; runs before the
  // scene starts the app thread)
  auto audioSession = AVAudioSession.sharedInstance;
  NSError* err = nil;
  if (![audioSession setCategory:AVAudioSessionCategoryPlayback error:&err])
    NSLog(@"audioSession setCategory failed: %@", err);

  err = nil;
  if (![audioSession setMode:AVAudioSessionModeMoviePlayback error:&err])
    NSLog(@"audioSession setMode failed: %@", err);

  err = nil;
  if (![audioSession setActive:YES error:&err])
    NSLog(@"audioSession setActive failed: %@", err);

  return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
                                   options:(UISceneConnectionOptions*)options
{
  // keep the name in sync with UIApplicationSceneManifest in Info.plist.in
  UISceneConfiguration* configuration =
      [[UISceneConfiguration alloc] initWithName:@"Default Configuration"
                                     sessionRole:connectingSceneSession.role];
  configuration.delegateClass = XBMCSceneDelegate.class;
  return configuration;
}

#pragma mark - Shutdown Procedures

- (void)applicationWillTerminate:(UIApplication*)application
{
  [self.xbmcController stopAnimation];
}

@end

@implementation XBMCSceneDelegate
{
  // applicationWillEnterForeground only ever followed applicationDidEnterBackground,
  // but sceneWillEnterForeground is also sent on a cold launch, before the app thread
  // has initialised. Only resume after we actually went to the background.
  BOOL _didEnterBackground;
}

- (XBMCController*)xbmcController
{
  return static_cast<XBMCController*>(self.window.rootViewController);
}

#pragma mark - Startup Procedures

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)connectionOptions
{
  if (![scene isKindOfClass:UIWindowScene.class])
    return;

  // UI setup
  self.window = [[UIWindow alloc] initWithWindowScene:static_cast<UIWindowScene*>(scene)];
  XBMCController* controller = [XBMCController new];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];

  static_cast<XBMCApplicationDelegate*>(UIApplication.sharedApplication.delegate).xbmcController =
      controller;

  [controller startAnimation];

  // launched from a top shelf item
  for (UIOpenURLContext* context in connectionOptions.URLContexts)
    HandleTopShelfURL(context.URL);
}

- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)URLContexts
{
  for (UIOpenURLContext* context in URLContexts)
    HandleTopShelfURL(context.URL);
}

#pragma mark - Lifecycle

- (void)sceneWillEnterForeground:(UIScene*)scene
{
  if (!_didEnterBackground)
    return;
  _didEnterBackground = NO;

  [self.xbmcController resumeAnimation];
  [self.xbmcController enterForeground];
}

- (void)sceneDidEnterBackground:(UIScene*)scene
{
  // Occurs when Kodi has been backgrounded
  // (e.g. when user uses remote to go to tvOS homescreen)
  _didEnterBackground = YES;
  if (scene.activationState == UISceneActivationStateBackground)
  {
    // the app is turn into background, not in by screen lock which has app state inactive.
    [self.xbmcController pauseAnimation];
    [self.xbmcController enterBackground];
  }
}

@end

static void SigPipeHandler(int s)
{
  NSLog(@"We Got a Pipe Signal: %d____________", s);
}

int main(int argc, char* argv[])
{
  @autoreleasepool
  {
    signal(SIGPIPE, SigPipeHandler);

    int retVal = 0;
    @try
    {
      retVal =
          UIApplicationMain(argc, argv, nil, NSStringFromClass([XBMCApplicationDelegate class]));
    }
    @catch (id theException)
    {
      ELOG(@"%@", theException);
    }
    @finally
    {
      ILOG(@"This always happens.");
    }

    return retVal;
  }
}
