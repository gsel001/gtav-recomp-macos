// gtav launcher (macOS) - GTAV.app: first-run setup window and game launcher.
//
//   open GTAV.app                 installed: start the game and quit; otherwise the setup window
//                                 (hold Option while opening it to get the setup window anyway)
//   GTAV --unattended [--home D]  setup from D/data/launcher.ini, no questions (testing)
//   GTAV --iphone [--home D]      iPhone build from the installed game folder, no window
//   GTAV --android [--home D]     Android pack (D/android/gtav-android-pack.zip), no window
//                                 (data/logs/iphone_unattended.txt; out: D/iphone, see install.h)
//   GTAV --home D                 use D as the game folder (default: ~/Library/Application
//                                 Support/GTAV Recomp, or the one picked in the window)
//   GTAV ... -- ARGS              pass ARGS to the game
//
// Setup is the same as on Windows and Linux (install.cpp): the player's own Disc 1, Disc 2 and
// Title Update 27 are copied and checked, the game executable is recompiled on this Mac into
// gtav_game.dylib with the bundled clang, then the game starts. The app bundle is read-only (it is
// signed); everything made goes to the game folder.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "install.h"
#include "sources.h"

namespace fs = std::filesystem;
using namespace loader;

namespace {

NSString* const kTitle = @"GTA V recomp";
NSString* const kRowLabels[3] = {@"Disc 1 (Install)", @"Disc 2 (Play)", @"Title Update 27"};
NSString* const kRowHints[3] = {@"ISO image or extracted folder", @"ISO image or extracted folder",
                                @"the TU file (tu00000009_00000000) or a folder"};

fs::path g_root;  // GTAV.app/Contents/Resources/gtav
fs::path g_home;  // game folder
std::vector<std::wstring> g_game_args;
std::atomic<bool> g_cancelled{false};

NSString* NS(const std::wstring& s) { return [NSString stringWithUTF8String:Narrow(s).c_str()]; }
NSString* NS(const fs::path& p) { return [NSString stringWithUTF8String:p.c_str()]; }

fs::path DefaultHome() {
  NSString* saved = [NSUserDefaults.standardUserDefaults stringForKey:@"GameFolder"];
  if (saved.length) return fs::path(saved.UTF8String);
  NSURL* support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                        inDomains:NSUserDomainMask]
                       .firstObject;
  return fs::path(support.path.UTF8String) / "GTAV Recomp";
}

fs::path SettingsPath() { return g_home / "data" / "launcher.ini"; }

std::vector<std::string> LoadInputs() {
  std::vector<std::string> v(3);
  std::ifstream f(SettingsPath());
  for (std::string& s : v) std::getline(f, s);
  return v;
}

void SaveInputs(const std::vector<std::string>& v) {
  std::error_code ec;
  fs::create_directories(g_home / "data", ec);
  std::ofstream f(SettingsPath());
  for (const std::string& s : v) f << s << "\n";
}

void ShowError(NSString* text) {
  NSAlert* a = [[NSAlert alloc] init];
  a.alertStyle = NSAlertStyleCritical;
  a.messageText = kTitle;
  a.informativeText = text;
  [a runModal];
}

bool StartGame(std::wstring* error) {
  return SyncRuntime(g_root, g_home, error) && LaunchGame(g_home, error, g_game_args);
}

// "--unattended": no window; installs from the three paths in <home>/data/launcher.ini (one per
// line), writes the stage and progress lines to data/logs/unattended.txt, says no to every
// question and returns 0 on success. Same contract as the Windows and Linux launchers.
class FileUi : public InstallUi {
 public:
  explicit FileUi(const fs::path& path) : f_(path, std::ios::app) {}
  void Stage(const std::wstring& t) override { Write(L"stage: " + t); }
  void Log(const std::wstring& t) override { Write(t); }
  void Progress(double f) override {
    int v = int(f * 100);
    if (v != last_) Write(L"progress: " + std::to_wstring(last_ = v) + L"%");
  }
  bool Cancelled() override { return false; }
  bool Ask(const std::wstring& q) override {
    Write(L"question (answered no): " + q);
    return false;
  }

 private:
  void Write(const std::wstring& s) {
    std::lock_guard lock(m_);
    f_ << Narrow(s) << "\n";
    f_.flush();
  }
  std::mutex m_;
  std::ofstream f_;
  int last_ = -1;
};

int Unattended() {
  std::error_code ec;
  fs::create_directories(g_home / "data" / "logs", ec);
  FileUi ui(g_home / "data" / "logs" / "unattended.txt");
  std::wstring error;
  if (!Preflight(g_root, &error) || !CheckGameFolder(g_home, &error)) {
    ui.Log(L"error: " + error);
    return 1;
  }
  std::vector<std::string> v = LoadInputs();
  if (v[0].empty() || v[1].empty() || v[2].empty()) {
    ui.Log(L"error: data/launcher.ini needs three lines: Disc 1, Disc 2, Title Update");
    return 1;
  }
  InstallInputs in{Widen(v[0]), Widen(v[1]), Widen(v[2])};
  bool ok = RunInstall(g_root, g_home, in, ui, &error) && SyncRuntime(g_root, g_home, &error);
  ui.Log(ok ? L"done" : L"error: " + error);
  return ok ? 0 : 1;
}

// "--iphone" / "--android": the phone builds without a window, for testing and for players who
// prefer Terminal (data/logs/<phone>_unattended.txt).
int PhoneUnattended(bool android) {
  std::error_code ec;
  fs::create_directories(g_home / "data" / "logs", ec);
  FileUi ui(g_home / "data" / "logs" / (android ? "android_unattended.txt" : "iphone_unattended.txt"));
  std::wstring error;
  bool ok = Preflight(g_root, &error) && (android ? RunAndroidBuild(g_root, g_home, ui, &error)
                                                  : RunIphoneBuild(g_root, g_home, ui, &error));
  ui.Log(ok ? L"done" : L"error: " + error);
  return ok ? 0 : 1;
}

}  // namespace

//-----------------------------------------------------------------------------
// Setup window
//-----------------------------------------------------------------------------

@interface SetupController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextFieldDelegate>
- (void)stage:(NSString*)s;
- (void)appendLog:(NSString*)s;
- (void)progress:(double)f;
- (bool)ask:(NSString*)q;
@end

// InstallUi for the window: called on the install thread, marshalled to the main thread.
class WindowUi : public InstallUi {
 public:
  explicit WindowUi(SetupController* c) : c_(c) {}
  void Stage(const std::wstring& t) override {
    NSString* s = NS(t);
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ stage:s]; });
  }
  void Log(const std::wstring& t) override {
    NSString* s = NS(t);
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ appendLog:s]; });
  }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_) return;
    last_ = v;
    dispatch_async(dispatch_get_main_queue(), ^{ [c_ progress:f]; });
  }
  bool Cancelled() override { return g_cancelled; }
  bool Ask(const std::wstring& q) override {
    NSString* s = NS(q);
    __block bool yes = false;
    dispatch_sync(dispatch_get_main_queue(), ^{ yes = [c_ ask:s]; });
    return yes;
  }

 private:
  __weak SetupController* c_;
  int last_ = -1;
};


@implementation SetupController {
  NSWindow* _window;
  NSTextField* _paths[3];
  NSTextField* _home;
  NSTextField* _stage;
  NSProgressIndicator* _progress;
  NSTextView* _log;
  NSButton* _install;
  NSButton* _iphone;
  NSButton* _android;
  NSButton* _play;
  NSButton* _browse[4];
  std::unique_ptr<WindowUi> _ui;
  bool _busy;
  bool _quit_when_stopped;
}

- (NSTextField*)label:(NSString*)text frame:(NSRect)r {
  NSTextField* t = [NSTextField wrappingLabelWithString:text];
  t.frame = r;
  [_window.contentView addSubview:t];
  return t;
}

- (NSButton*)button:(NSString*)title frame:(NSRect)r action:(SEL)sel {
  NSButton* b = [NSButton buttonWithTitle:title target:self action:sel];
  b.frame = r;
  [_window.contentView addSubview:b];
  return b;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
  const CGFloat w = 720, h = 560;
  _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = @"GTA V recomp setup";
  _window.delegate = self;
  [self label:@"Point setup at your own copy of the game: both Xbox 360 discs and Title Update 27. "
              @"Setup copies the game data into the game folder, recompiles the game executable on "
              @"this Mac and checks every file. Nothing is downloaded and nothing is sent anywhere. "
              @"It needs about 17 GB and 15-40 minutes."
        frame:NSMakeRect(16, h - 68, w - 32, 56)];

  std::vector<std::string> saved = LoadInputs();
  for (int i = 0; i < 4; ++i) {
    CGFloat y = h - 110 - i * 52;
    NSString* label = i < 3 ? kRowLabels[i] : @"Game folder";
    NSString* hint = i < 3 ? kRowHints[i] : @"where the game data and the built game go (17 GB)";
    NSTextField* l = [self label:[NSString stringWithFormat:@"%@ - %@", label, hint]
                           frame:NSMakeRect(16, y + 22, w - 32, 18)];
    l.textColor = NSColor.secondaryLabelColor;
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(16, y - 4, w - 140, 24)];
    f.stringValue = i < 3 ? [NSString stringWithUTF8String:saved[i].c_str()] : NS(g_home);
    f.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [_window.contentView addSubview:f];
    if (i < 3)
      _paths[i] = f;
    else
      _home = f, f.delegate = self;
    _browse[i] = [self button:@"Choose..." frame:NSMakeRect(w - 116, y - 6, 100, 28) action:@selector(choose:)];
    _browse[i].tag = i;
  }

  _stage = [self label:@"" frame:NSMakeRect(16, h - 340, w - 32, 20)];
  _progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16, h - 366, w - 32, 20)];
  _progress.indeterminate = NO;
  _progress.minValue = 0;
  _progress.maxValue = 1;
  [_window.contentView addSubview:_progress];

  NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 56, w - 32, h - 432)];
  sv.hasVerticalScroller = YES;
  sv.borderType = NSBezelBorder;
  _log = [[NSTextView alloc] initWithFrame:sv.contentView.bounds];
  _log.editable = NO;
  _log.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
  _log.autoresizingMask = NSViewWidthSizable;
  sv.documentView = _log;
  [_window.contentView addSubview:sv];

  _iphone = [self button:@"Build for iPhone" frame:NSMakeRect(16, 14, 150, 32) action:@selector(iphone:)];
  _android = [self button:@"Build for Android" frame:NSMakeRect(172, 14, 160, 32) action:@selector(android:)];
  _install = [self button:@"Install" frame:NSMakeRect(w - 236, 14, 104, 32) action:@selector(install:)];
  _play = [self button:@"Play" frame:NSMakeRect(w - 120, 14, 104, 32) action:@selector(play:)];
  _play.keyEquivalent = @"\r";
  [self setBusy:false];
  _stage.stringValue = IsInstalled(g_home) ? @"Installed. Press Play, or Install to redo setup."
                                            : @"Pick the three inputs, then press Install.";
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (void)setBusy:(bool)busy {
  _busy = busy;
  for (int i = 0; i < 4; ++i) _browse[i].enabled = !busy;
  for (int i = 0; i < 3; ++i) _paths[i].editable = !busy;
  _home.editable = !busy;
  _install.title = busy ? @"Cancel" : @"Install";
  _iphone.enabled = !busy && IsInstalled(g_home);
  _android.enabled = !busy && IsInstalled(g_home);
  _play.enabled = !busy && IsInstalled(g_home);
}

- (void)choose:(NSButton*)sender {
  NSOpenPanel* p = [NSOpenPanel openPanel];
  int i = int(sender.tag);
  p.canChooseDirectories = YES;
  p.canChooseFiles = i < 3;
  p.canCreateDirectories = i == 3;
  p.message = i < 3 ? [NSString stringWithFormat:@"%@: %@", kRowLabels[i], kRowHints[i]]
                    : @"Folder for the game data and the built game (about 17 GB)";
  if ([p runModal] != NSModalResponseOK) return;
  (i < 3 ? _paths[i] : _home).stringValue = p.URL.path;
  if (i == 3) [self homeChanged];
}

// A game folder that already holds an install can be played right away.
- (void)homeChanged {
  fs::path home = _home.stringValue.UTF8String;
  if (home.empty() || !home.is_absolute()) return;
  g_home = home;
  [NSUserDefaults.standardUserDefaults setObject:_home.stringValue forKey:@"GameFolder"];
  _play.enabled = !_busy && IsInstalled(g_home);
  _stage.stringValue = IsInstalled(g_home) ? @"Installed. Press Play, or Install to redo setup."
                                            : @"Pick the three inputs, then press Install.";
}

- (void)controlTextDidEndEditing:(NSNotification*)n {
  if (n.object == _home) [self homeChanged];
}

- (void)appendLog:(NSString*)s {
  [_log.textStorage appendAttributedString:[[NSAttributedString alloc]
                                               initWithString:[s stringByAppendingString:@"\n"]
                                                   attributes:@{NSFontAttributeName : _log.font,
                                                                NSForegroundColorAttributeName :
                                                                    NSColor.textColor}]];
  [_log scrollToEndOfDocument:nil];
}

- (void)stage:(NSString*)s {
  _stage.stringValue = s;
}

- (void)progress:(double)f {
  _progress.doubleValue = f;
}

- (bool)ask:(NSString*)q {
  NSAlert* a = [[NSAlert alloc] init];
  a.messageText = @"GTA V recomp setup";
  a.informativeText = q;
  [a addButtonWithTitle:@"Yes"];
  [a addButtonWithTitle:@"No"];
  return [a runModal] == NSAlertFirstButtonReturn;
}

- (void)play:(id)sender {
  std::wstring error;
  if (StartGame(&error)) {
    [NSApp terminate:nil];
    return;
  }
  ShowError(NS(error));
}

- (BOOL)windowShouldClose:(NSWindow*)w {
  if (!_busy) return YES;
  if (![self ask:@"Setup is running. Stop it and close? Running setup again continues where it stopped."])
    return NO;
  g_cancelled = true;
  _quit_when_stopped = true;
  return NO;  // quits when the install thread has stopped (finished:)
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  return YES;
}

// Phone builds (install.h RunIphoneBuild / RunAndroidBuild) from the installed game folder;
// Install's button cancels.
- (void)iphone:(id)sender {
  [self phone:false];
}
- (void)android:(id)sender {
  [self phone:true];
}
- (void)phone:(bool)android {
  if (_busy) return;
  _ui = std::make_unique<WindowUi>(self);
  g_cancelled = false;
  [self setBusy:true];
  WindowUi* ui = _ui.get();
  std::thread([self, ui, android] {
    std::wstring err;
    bool ok = android ? RunAndroidBuild(g_root, g_home, *ui, &err) : RunIphoneBuild(g_root, g_home, *ui, &err);
    NSString* e = NS(err);
    dispatch_async(dispatch_get_main_queue(), ^{
      [self setBusy:false];
      if (ok) {
        [self stage:android ? @"Android build finished: copy gtav-android-pack.zip and your disc files to the phone."
                            : @"iPhone build finished: install GTAV.ipa with Sideloadly or AltStore."];
        // Show the result in Finder: the next step is dragging it to the phone / into Sideloadly.
        fs::path out = android ? g_home / "android" / "gtav-android-pack.zip" : g_home / "iphone" / "GTAV.ipa";
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:NS(out)] ]];
      } else if (g_cancelled) {
        [self stage:@"Cancelled."];
      } else {
        [self stage:android ? @"Android build stopped." : @"iPhone build stopped."];
        ShowError(e);
      }
    });
  }).detach();
}

- (void)install:(id)sender {
  if (_busy) {
    g_cancelled = true;
    _stage.stringValue = @"Cancelling...";
    return;
  }
  std::vector<std::string> paths(3);
  for (int i = 0; i < 3; ++i) {
    paths[i] = _paths[i].stringValue.UTF8String;
    std::error_code ec;
    if (paths[i].empty() || !fs::exists(paths[i], ec)) {
      ShowError([NSString stringWithFormat:@"%@: pick the image, file or folder.", kRowLabels[i]]);
      return;
    }
  }
  fs::path home = _home.stringValue.UTF8String;
  std::wstring error;
  if (home.empty() || !home.is_absolute() || !CheckGameFolder(home, &error)) {
    ShowError(error.empty() ? @"Pick a game folder." : NS(error));
    return;
  }
  g_home = home;
  [NSUserDefaults.standardUserDefaults setObject:_home.stringValue forKey:@"GameFolder"];
  SaveInputs(paths);

  _ui = std::make_unique<WindowUi>(self);
  g_cancelled = false;
  [self setBusy:true];
  InstallInputs in{Widen(paths[0]), Widen(paths[1]), Widen(paths[2])};
  WindowUi* ui = _ui.get();
  std::thread([self, in, ui] {
    std::wstring err;
    bool ok = RunInstall(g_root, g_home, in, *ui, &err);
    NSString* e = NS(err);
    dispatch_async(dispatch_get_main_queue(), ^{ [self finished:ok error:e]; });
  }).detach();
}

- (void)finished:(bool)ok error:(NSString*)error {
  [self setBusy:false];
  if (ok) {
    _stage.stringValue = @"Setup finished. Press Play.";
    return;
  }
  _stage.stringValue = g_cancelled ? @"Cancelled. Install again to continue where it stopped."
                                   : @"Setup stopped.";
  if (g_cancelled) {
    if (_quit_when_stopped) [NSApp terminate:nil];
    return;
  }
  ShowError(error);
}

@end

int main(int argc, char** argv) {
  // Input-attachment resolves alternate lit/dark frames on M1 / MoltenVK. Disable
  // them before the game creates its render resources, as on iOS; an explicit
  // GTAV_NATIVE_FETCH_RESOLVE=1 still allows testing the optimization.
  setenv("GTAV_NATIVE_FETCH_RESOLVE", "0", 0);
  @autoreleasepool {
    g_root = fs::path(NSBundle.mainBundle.resourcePath.UTF8String) / "gtav";
    g_home = DefaultHome();
    bool unattended = false, setup = false, iphone = false, android = false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--") {
        for (++i; i < argc; ++i) g_game_args.push_back(Widen(argv[i]));
        break;
      }
      if (a == "--unattended") {
        unattended = true;
      } else if (a == "--iphone") {
        iphone = true;
      } else if (a == "--android") {
        android = true;
      } else if (a == "--setup") {
        setup = true;
      } else if (a == "--home" && i + 1 < argc) {
        g_home = fs::absolute(argv[++i]);
      } else if (a == "--root" && i + 1 < argc) {
        g_root = fs::absolute(argv[++i]);  // a package folder outside an app bundle (testing)
      }
      // Anything else (e.g. -psn_* from old Finder launches) is ignored.
    }
    if (unattended) return Unattended();
    if (iphone || android) return PhoneUnattended(android);

    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    bool option = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
    std::wstring error;
    // The common case: installed, start the game and get out of the way.
    if (!setup && !option && IsInstalled(g_home)) {
      if (StartGame(&error)) return 0;
      ShowError(NS(error));
    }
    if (!Preflight(g_root, &error)) {
      ShowError(NS(error));
      return 1;
    }
    SetupController* c = [[SetupController alloc] init];
    NSApp.delegate = c;
    [NSApp run];
  }
  return 0;
}
