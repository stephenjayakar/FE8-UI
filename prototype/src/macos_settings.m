#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_settings.h"

static NSString *const kAudioKey = @"FE8AudioEnabled";
static NSString *const kVSyncKey = @"FE8VSyncEnabled";
static NSString *const kExtensionsKey = @"FE8ExtensionsEnabled";
static NSString *const kShaderKey = @"FE8ShaderMode";

enum { FE8_HOTKEY_TAG_BASE = 100 };

static NSString *bindingKey(enum Fe8HostButton button) {
    return [NSString stringWithFormat:@"FE8Binding.%s", fe8_host_button_name(button)];
}

static NSString *hotkeyBindingKey(enum Fe8HostHotkey hotkey) {
    return [NSString stringWithFormat:@"FE8Hotkey.%s", fe8_host_hotkey_name(hotkey)];
}

static SDL_Scancode scancodeForEvent(NSEvent *event) {
    if (event.type == NSEventTypeFlagsChanged) {
        NSEventModifierFlags flags = event.modifierFlags &
            NSEventModifierFlagDeviceIndependentFlagsMask;
        switch (event.keyCode) {
        case 56: return flags & NSEventModifierFlagShift ?
            SDL_SCANCODE_LSHIFT : SDL_SCANCODE_UNKNOWN;
        case 60: return flags & NSEventModifierFlagShift ?
            SDL_SCANCODE_RSHIFT : SDL_SCANCODE_UNKNOWN;
        case 59: return flags & NSEventModifierFlagControl ?
            SDL_SCANCODE_LCTRL : SDL_SCANCODE_UNKNOWN;
        case 62: return flags & NSEventModifierFlagControl ?
            SDL_SCANCODE_RCTRL : SDL_SCANCODE_UNKNOWN;
        case 58: return flags & NSEventModifierFlagOption ?
            SDL_SCANCODE_LALT : SDL_SCANCODE_UNKNOWN;
        case 61: return flags & NSEventModifierFlagOption ?
            SDL_SCANCODE_RALT : SDL_SCANCODE_UNKNOWN;
        case 55: return flags & NSEventModifierFlagCommand ?
            SDL_SCANCODE_LGUI : SDL_SCANCODE_UNKNOWN;
        case 54: return flags & NSEventModifierFlagCommand ?
            SDL_SCANCODE_RGUI : SDL_SCANCODE_UNKNOWN;
        case 57: return SDL_SCANCODE_CAPSLOCK;
        default: return SDL_SCANCODE_UNKNOWN;
        }
    }
    NSString *characters = event.charactersIgnoringModifiers;
    if (!characters.length)
        return SDL_SCANCODE_UNKNOWN;
    unichar character = [characters characterAtIndex:0];
    switch (character) {
    case NSUpArrowFunctionKey: return SDL_SCANCODE_UP;
    case NSDownArrowFunctionKey: return SDL_SCANCODE_DOWN;
    case NSLeftArrowFunctionKey: return SDL_SCANCODE_LEFT;
    case NSRightArrowFunctionKey: return SDL_SCANCODE_RIGHT;
    case NSHomeFunctionKey: return SDL_SCANCODE_HOME;
    case NSEndFunctionKey: return SDL_SCANCODE_END;
    case NSPageUpFunctionKey: return SDL_SCANCODE_PAGEUP;
    case NSPageDownFunctionKey: return SDL_SCANCODE_PAGEDOWN;
    case NSInsertFunctionKey: return SDL_SCANCODE_INSERT;
    case NSDeleteFunctionKey: return SDL_SCANCODE_DELETE;
    case NSF1FunctionKey: return SDL_SCANCODE_F1;
    case NSF2FunctionKey: return SDL_SCANCODE_F2;
    case NSF3FunctionKey: return SDL_SCANCODE_F3;
    case NSF4FunctionKey: return SDL_SCANCODE_F4;
    case NSF5FunctionKey: return SDL_SCANCODE_F5;
    case NSF6FunctionKey: return SDL_SCANCODE_F6;
    case NSF7FunctionKey: return SDL_SCANCODE_F7;
    case NSF8FunctionKey: return SDL_SCANCODE_F8;
    case NSF9FunctionKey: return SDL_SCANCODE_F9;
    case NSF10FunctionKey: return SDL_SCANCODE_F10;
    case NSF11FunctionKey: return SDL_SCANCODE_F11;
    case NSF12FunctionKey: return SDL_SCANCODE_F12;
    case NSDeleteCharacter: return SDL_SCANCODE_BACKSPACE;
    case NSCarriageReturnCharacter:
    case NSEnterCharacter: return SDL_SCANCODE_RETURN;
    case NSTabCharacter:
    case NSBackTabCharacter: return SDL_SCANCODE_TAB;
    case ' ': return SDL_SCANCODE_SPACE;
    default:
        break;
    }
    if (character < 128) {
        if (character >= 'A' && character <= 'Z')
            character = (unichar)(character - 'A' + 'a');
        return SDL_GetScancodeFromKey((SDL_Keycode)character);
    }
    return SDL_SCANCODE_UNKNOWN;
}

@interface Fe8SettingsController : NSObject <NSWindowDelegate>
@property(nonatomic, assign) Fe8HostSettings *settings;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSMutableArray<NSButton *> *bindingButtons;
@property(nonatomic, strong) NSButton *listeningButton;
@property(nonatomic, strong) id keyMonitor;
@property(nonatomic, assign) void *stateContext;
@property(nonatomic, assign) Fe8HostStateCallback saveState;
@property(nonatomic, assign) Fe8HostStateCallback loadState;
@property(nonatomic, copy) NSString *quickStatePath;
- (instancetype)initWithSettings:(Fe8HostSettings *)settings
    stateContext:(void *)stateContext
    saveState:(Fe8HostStateCallback)saveState
    loadState:(Fe8HostStateCallback)loadState
    quickStatePath:(NSString *)quickStatePath;
- (void)showSettings:(id)sender;
@end

@implementation Fe8SettingsController

- (NSString *)titleForBindingTag:(NSInteger)tag {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (tag >= 0 && tag < FE8_HOST_BUTTON_COUNT)
        scancode = self.settings->bindings[tag];
    else if (tag >= FE8_HOTKEY_TAG_BASE &&
            tag < FE8_HOTKEY_TAG_BASE + FE8_HOST_HOTKEY_COUNT)
        scancode = self.settings->hotkeys[tag - FE8_HOTKEY_TAG_BASE];
    const char *name = SDL_GetScancodeName(scancode);
    return name && *name ? [NSString stringWithUTF8String:name] : @"Unbound";
}

- (void)stopListening {
    if (self.keyMonitor) {
        [NSEvent removeMonitor:self.keyMonitor];
        self.keyMonitor = nil;
    }
    if (self.listeningButton) {
        NSInteger tag = self.listeningButton.tag;
        self.listeningButton.title = [self titleForBindingTag:tag];
        self.listeningButton = nil;
    }
}

- (void)captureBinding:(NSButton *)sender {
    [self stopListening];
    self.listeningButton = sender;
    sender.title = @"Press a key…  (Esc cancels)";
    [self.window makeFirstResponder:sender];
    self.keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskKeyDown | NSEventMaskFlagsChanged
        handler:^NSEvent *(NSEvent *event) {
            NSString *characters = event.type == NSEventTypeKeyDown ?
                event.charactersIgnoringModifiers : nil;
            if (characters.length && [characters characterAtIndex:0] == 0x1B) {
                [self stopListening];
                return nil;
            }
            SDL_Scancode scancode = scancodeForEvent(event);
            NSInteger tag = self.listeningButton.tag;
            if (scancode != SDL_SCANCODE_UNKNOWN && tag >= 0 &&
                    tag < FE8_HOST_BUTTON_COUNT) {
                self.settings->bindings[tag] = scancode;
                ++self.settings->revision;
                [NSUserDefaults.standardUserDefaults setInteger:scancode
                    forKey:bindingKey((enum Fe8HostButton)tag)];
                [self stopListening];
            } else if (scancode != SDL_SCANCODE_UNKNOWN &&
                    tag >= FE8_HOTKEY_TAG_BASE &&
                    tag < FE8_HOTKEY_TAG_BASE + FE8_HOST_HOTKEY_COUNT) {
                enum Fe8HostHotkey hotkey =
                    (enum Fe8HostHotkey)(tag - FE8_HOTKEY_TAG_BASE);
                self.settings->hotkeys[hotkey] = scancode;
                ++self.settings->revision;
                [NSUserDefaults.standardUserDefaults setInteger:scancode
                    forKey:hotkeyBindingKey(hotkey)];
                [self stopListening];
            }
            return nil;
        }];
}

- (void)settingChanged:(NSButton *)sender {
    int enabled = sender.state == NSControlStateValueOn;
    switch (sender.tag) {
    case 0: self.settings->audio_enabled = enabled; break;
    case 1: self.settings->vsync_enabled = enabled; break;
    case 2: self.settings->extensions_enabled = enabled; break;
    default: return;
    }
    ++self.settings->revision;
    NSString *key = sender.tag == 0 ? kAudioKey :
        (sender.tag == 1 ? kVSyncKey : kExtensionsKey);
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:key];
}

- (void)shaderChanged:(NSPopUpButton *)sender {
    NSInteger shader = sender.indexOfSelectedItem;
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        return;
    self.settings->shader = (enum Fe8HostShader)shader;
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:shader forKey:kShaderKey];
}

- (instancetype)initWithSettings:(Fe8HostSettings *)settings
    stateContext:(void *)stateContext
    saveState:(Fe8HostStateCallback)saveState
    loadState:(Fe8HostStateCallback)loadState
    quickStatePath:(NSString *)quickStatePath {
    self = [super init];
    if (!self)
        return nil;
    self.settings = settings;
    self.stateContext = stateContext;
    self.saveState = saveState;
    self.loadState = loadState;
    self.quickStatePath = quickStatePath;
    self.bindingButtons = [NSMutableArray array];
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 430, 680)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"FE8 Frontend Settings";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;

    NSView *content = self.window.contentView;
    NSArray<NSString *> *optionNames = @[
        @"Enable audio", @"Synchronize presentation (VSync)", @"Enable extended renderer"
    ];
    int optionValues[] = {
        settings->audio_enabled, settings->vsync_enabled, settings->extensions_enabled
    };
    NSInteger i;
    for (i = 0; i < (NSInteger)optionNames.count; ++i) {
        NSButton *check = [[NSButton alloc]
            initWithFrame:NSMakeRect(24, 635 - i * 30, 380, 24)];
        check.buttonType = NSButtonTypeSwitch;
        check.title = optionNames[i];
        check.state = optionValues[i] ? NSControlStateValueOn : NSControlStateValueOff;
        check.tag = i;
        check.target = self;
        check.action = @selector(settingChanged:);
        [content addSubview:check];
    }

    NSTextField *shaderLabel = [NSTextField labelWithString:@"Video shader"];
    shaderLabel.frame = NSMakeRect(30, 529, 90, 24);
    [content addSubview:shaderLabel];
    NSPopUpButton *shaderPopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(130, 525, 265, 28) pullsDown:NO];
    for (i = 0; i < FE8_HOST_SHADER_COUNT; ++i)
        [shaderPopup addItemWithTitle:[NSString stringWithUTF8String:
            fe8_host_shader_name((enum Fe8HostShader)i)]];
    [shaderPopup selectItemAtIndex:settings->shader];
    shaderPopup.target = self;
    shaderPopup.action = @selector(shaderChanged:);
    [content addSubview:shaderPopup];

    NSTextField *heading = [NSTextField labelWithString:@"Keyboard controls"];
    heading.frame = NSMakeRect(24, 495, 380, 24);
    heading.font = [NSFont boldSystemFontOfSize:13];
    [content addSubview:heading];

    NSTextField *hint = [NSTextField labelWithString:
        @"Click a binding, then press the key you want to use."];
    hint.frame = NSMakeRect(24, 470, 380, 20);
    hint.textColor = NSColor.secondaryLabelColor;
    [content addSubview:hint];

    for (i = 0; i < FE8_HOST_BUTTON_COUNT; ++i) {
        CGFloat y = 438 - i * 29;
        NSTextField *label = [NSTextField labelWithString:[NSString stringWithUTF8String:
            fe8_host_button_name((enum Fe8HostButton)i)]];
        label.frame = NSMakeRect(30, y + 4, 90, 20);
        [content addSubview:label];

        NSButton *binding = [NSButton buttonWithTitle:
            [self titleForBindingTag:i]
            target:self action:@selector(captureBinding:)];
        binding.frame = NSMakeRect(130, y, 265, 26);
        binding.tag = i;
        binding.bezelStyle = NSBezelStyleRounded;
        [self.bindingButtons addObject:binding];
        [content addSubview:binding];
    }

    NSTextField *hotkeyHeading = [NSTextField labelWithString:@"Hotkeys"];
    hotkeyHeading.frame = NSMakeRect(24, 140, 380, 24);
    hotkeyHeading.font = [NSFont boldSystemFontOfSize:13];
    [content addSubview:hotkeyHeading];
    for (i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) {
        CGFloat y = 108 - i * 29;
        NSInteger tag = FE8_HOTKEY_TAG_BASE + i;
        NSTextField *label = [NSTextField labelWithString:[NSString stringWithUTF8String:
            fe8_host_hotkey_name((enum Fe8HostHotkey)i)]];
        label.frame = NSMakeRect(30, y + 4, 90, 20);
        [content addSubview:label];
        NSButton *binding = [NSButton buttonWithTitle:[self titleForBindingTag:tag]
            target:self action:@selector(captureBinding:)];
        binding.frame = NSMakeRect(130, y, 265, 26);
        binding.tag = tag;
        binding.bezelStyle = NSBezelStyleRounded;
        [self.bindingButtons addObject:binding];
        [content addSubview:binding];
    }
    [self.window center];
    return self;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    [self stopListening];
}

- (void)showSettings:(id)sender {
    (void)sender;
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activate];
}

- (BOOL)performStateCallback:(Fe8HostStateCallback)callback path:(NSString *)path {
    if (!callback || !path.length)
        return NO;
    BOOL success = callback(self.stateContext, path.fileSystemRepresentation) != 0;
    if (!success) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"State operation failed";
        alert.informativeText = [NSString stringWithFormat:
            @"The state could not be read or written at:\n%@", path];
        [alert runModal];
    }
    return success;
}

- (void)saveStateAs:(id)sender {
    (void)sender;
    NSSavePanel *panel = NSSavePanel.savePanel;
    panel.title = @"Save Emulator State";
    panel.nameFieldStringValue = self.quickStatePath.lastPathComponent.length ?
        self.quickStatePath.lastPathComponent : @"quick-state.ss";
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"ss"]];
    if ([panel runModal] == NSModalResponseOK &&
            [self performStateCallback:self.saveState path:panel.URL.path])
        self.quickStatePath = panel.URL.path;
}

- (void)loadStateFrom:(id)sender {
    (void)sender;
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.title = @"Load Emulator State";
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"ss"],
        [UTType typeWithFilenameExtension:@"ss1"]];
    if ([panel runModal] == NSModalResponseOK &&
            [self performStateCallback:self.loadState path:panel.URL.path])
        self.quickStatePath = panel.URL.path;
}

- (void)quickSaveState:(id)sender {
    (void)sender;
    if (self.quickStatePath.length)
        [self performStateCallback:self.saveState path:self.quickStatePath];
    else
        [self saveStateAs:nil];
}

- (void)quickLoadState:(id)sender {
    (void)sender;
    if (self.quickStatePath.length &&
            [NSFileManager.defaultManager fileExistsAtPath:self.quickStatePath])
        [self performStateCallback:self.loadState path:self.quickStatePath];
    else
        [self loadStateFrom:nil];
}

@end

static Fe8SettingsController *settingsController;

void fe8_macos_load_settings(Fe8HostSettings *settings) {
    @autoreleasepool {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        [defaults registerDefaults:@{
            kAudioKey: @YES,
            kVSyncKey: @YES,
            kExtensionsKey: @YES,
            kShaderKey: @(FE8_HOST_SHADER_OFF),
        }];
        settings->audio_enabled = [defaults boolForKey:kAudioKey];
        settings->vsync_enabled = [defaults boolForKey:kVSyncKey];
        settings->extensions_enabled = [defaults boolForKey:kExtensionsKey];
        NSInteger shader = [defaults integerForKey:kShaderKey];
        settings->shader = shader >= 0 && shader < FE8_HOST_SHADER_COUNT ?
            (enum Fe8HostShader)shader : FE8_HOST_SHADER_OFF;
        for (int button = 0; button < FE8_HOST_BUTTON_COUNT; ++button) {
            NSString *key = bindingKey((enum Fe8HostButton)button);
            if ([defaults objectForKey:key])
                settings->bindings[button] =
                    (SDL_Scancode)[defaults integerForKey:key];
        }
        for (int hotkey = 0; hotkey < FE8_HOST_HOTKEY_COUNT; ++hotkey) {
            NSString *key = hotkeyBindingKey((enum Fe8HostHotkey)hotkey);
            if ([defaults objectForKey:key])
                settings->hotkeys[hotkey] =
                    (SDL_Scancode)[defaults integerForKey:key];
        }
    }
}

static NSMenuItem *stateMenuItem(NSString *title, SEL action,
    NSString *keyEquivalent, NSEventModifierFlags modifiers) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
        action:action keyEquivalent:keyEquivalent];
    item.keyEquivalentModifierMask = modifiers;
    item.target = settingsController;
    return item;
}

void fe8_macos_install_settings_menu(
    Fe8HostSettings *settings,
    void *state_context,
    Fe8HostStateCallback save_state,
    Fe8HostStateCallback load_state,
    const char *quick_state_path) {
    @autoreleasepool {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSString *path = quick_state_path ?
            [NSString stringWithUTF8String:quick_state_path] : nil;
        settingsController = [[Fe8SettingsController alloc]
            initWithSettings:settings
            stateContext:state_context
            saveState:save_state
            loadState:load_state
            quickStatePath:path];
        NSMenu *mainMenu = NSApp.mainMenu;
        if (!mainMenu) {
            mainMenu = [[NSMenu alloc] initWithTitle:@""];
            NSApp.mainMenu = mainMenu;
        }

        if (save_state && load_state) {
            NSMenuItem *stateRoot = [[NSMenuItem alloc]
                initWithTitle:@"State" action:nil keyEquivalent:@""];
            NSMenu *stateMenu = [[NSMenu alloc] initWithTitle:@"State"];
            [stateMenu addItem:stateMenuItem(@"Quick Save State", @selector(quickSaveState:),
                @"", 0)];
            [stateMenu addItem:stateMenuItem(@"Quick Load State", @selector(quickLoadState:),
                @"", 0)];
            [stateMenu addItem:NSMenuItem.separatorItem];
            [stateMenu addItem:stateMenuItem(@"Save State As…", @selector(saveStateAs:),
                @"s", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
            [stateMenu addItem:stateMenuItem(@"Load State…", @selector(loadStateFrom:),
                @"l", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
            stateRoot.submenu = stateMenu;
            [mainMenu addItem:stateRoot];
        }

        NSMenuItem *settingsRoot = [[NSMenuItem alloc]
            initWithTitle:@"Settings" action:nil keyEquivalent:@""];
        NSMenu *settingsMenu = [[NSMenu alloc] initWithTitle:@"Settings"];
        NSMenuItem *open = [[NSMenuItem alloc] initWithTitle:@"Settings…"
            action:@selector(showSettings:) keyEquivalent:@","];
        open.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        open.target = settingsController;
        [settingsMenu addItem:open];
        settingsRoot.submenu = settingsMenu;
        [mainMenu addItem:settingsRoot];
    }
}
