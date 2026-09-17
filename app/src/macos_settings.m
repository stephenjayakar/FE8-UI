#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_settings.h"

static NSString *const kAudioKey = @"FE8AudioEnabled";
static NSString *const kVSyncKey = @"FE8VSyncEnabled";
static NSString *const kExtensionsKey = @"FE8ExtensionsEnabled";
static NSString *const kVoxelKey = @"FE8VoxelEnabled";
static NSString *const kVoxelGpuKey = @"FE8VoxelGpu";
static NSString *const kMouseKey = @"FE8MouseEnabled";
static NSString *const kShaderKey = @"FE8ShaderMode";
static NSString *const kZoomSensitivityKey = @"FE8ZoomSensitivity";
static NSString *const kSpeedupRateKey = @"FE8SpeedupRate";

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
    /* Bind physical keys, just like SDL_Keysym.scancode in the game. The
     * library has no SDL video subsystem or initialized keymap; converting
     * characters with SDL_GetScancodeFromKey silently rejected letters there.
     * Cocoa virtual key positions also avoid layout/Shift-dependent bindings. */
    if (event.type != NSEventTypeKeyDown) return SDL_SCANCODE_UNKNOWN;
    unsigned code = event.keyCode;
    if ((code == 10 || code == 50) && KBGetLayoutType(LMGetKbdType()) == kKeyboardISO)
        code = code == 10 ? 50 : 10;
    static const SDL_Scancode keys[128] = {
        [0] = SDL_SCANCODE_A, [1] = SDL_SCANCODE_S, [2] = SDL_SCANCODE_D,
        [3] = SDL_SCANCODE_F, [4] = SDL_SCANCODE_H, [5] = SDL_SCANCODE_G,
        [6] = SDL_SCANCODE_Z, [7] = SDL_SCANCODE_X, [8] = SDL_SCANCODE_C,
        [9] = SDL_SCANCODE_V, [10] = SDL_SCANCODE_NONUSBACKSLASH, [11] = SDL_SCANCODE_B,
        [12] = SDL_SCANCODE_Q, [13] = SDL_SCANCODE_W, [14] = SDL_SCANCODE_E,
        [15] = SDL_SCANCODE_R, [16] = SDL_SCANCODE_Y, [17] = SDL_SCANCODE_T,
        [18] = SDL_SCANCODE_1, [19] = SDL_SCANCODE_2, [20] = SDL_SCANCODE_3,
        [21] = SDL_SCANCODE_4, [22] = SDL_SCANCODE_6, [23] = SDL_SCANCODE_5,
        [24] = SDL_SCANCODE_EQUALS, [25] = SDL_SCANCODE_9, [26] = SDL_SCANCODE_7,
        [27] = SDL_SCANCODE_MINUS, [28] = SDL_SCANCODE_8, [29] = SDL_SCANCODE_0,
        [30] = SDL_SCANCODE_RIGHTBRACKET, [31] = SDL_SCANCODE_O, [32] = SDL_SCANCODE_U,
        [33] = SDL_SCANCODE_LEFTBRACKET, [34] = SDL_SCANCODE_I, [35] = SDL_SCANCODE_P,
        [36] = SDL_SCANCODE_RETURN, [37] = SDL_SCANCODE_L, [38] = SDL_SCANCODE_J,
        [39] = SDL_SCANCODE_APOSTROPHE, [40] = SDL_SCANCODE_K, [41] = SDL_SCANCODE_SEMICOLON,
        [42] = SDL_SCANCODE_BACKSLASH, [43] = SDL_SCANCODE_COMMA, [44] = SDL_SCANCODE_SLASH,
        [45] = SDL_SCANCODE_N, [46] = SDL_SCANCODE_M, [47] = SDL_SCANCODE_PERIOD,
        [48] = SDL_SCANCODE_TAB, [49] = SDL_SCANCODE_SPACE, [50] = SDL_SCANCODE_GRAVE,
        [51] = SDL_SCANCODE_BACKSPACE, [52] = SDL_SCANCODE_KP_ENTER, [53] = SDL_SCANCODE_ESCAPE,
        [64] = SDL_SCANCODE_F17, [65] = SDL_SCANCODE_KP_PERIOD, [67] = SDL_SCANCODE_KP_MULTIPLY,
        [69] = SDL_SCANCODE_KP_PLUS, [71] = SDL_SCANCODE_NUMLOCKCLEAR, [72] = SDL_SCANCODE_VOLUMEUP,
        [73] = SDL_SCANCODE_VOLUMEDOWN, [74] = SDL_SCANCODE_MUTE, [75] = SDL_SCANCODE_KP_DIVIDE,
        [76] = SDL_SCANCODE_KP_ENTER, [78] = SDL_SCANCODE_KP_MINUS, [79] = SDL_SCANCODE_F18,
        [80] = SDL_SCANCODE_F19, [81] = SDL_SCANCODE_KP_EQUALS, [82] = SDL_SCANCODE_KP_0,
        [83] = SDL_SCANCODE_KP_1, [84] = SDL_SCANCODE_KP_2, [85] = SDL_SCANCODE_KP_3,
        [86] = SDL_SCANCODE_KP_4, [87] = SDL_SCANCODE_KP_5, [88] = SDL_SCANCODE_KP_6,
        [89] = SDL_SCANCODE_KP_7, [91] = SDL_SCANCODE_KP_8, [92] = SDL_SCANCODE_KP_9,
        [93] = SDL_SCANCODE_INTERNATIONAL3, [94] = SDL_SCANCODE_INTERNATIONAL1, [95] = SDL_SCANCODE_KP_COMMA,
        [96] = SDL_SCANCODE_F5, [97] = SDL_SCANCODE_F6, [98] = SDL_SCANCODE_F7,
        [99] = SDL_SCANCODE_F3, [100] = SDL_SCANCODE_F8, [101] = SDL_SCANCODE_F9,
        [102] = SDL_SCANCODE_LANG2, [103] = SDL_SCANCODE_F11, [104] = SDL_SCANCODE_LANG1,
        [105] = SDL_SCANCODE_PRINTSCREEN, [106] = SDL_SCANCODE_F16, [107] = SDL_SCANCODE_SCROLLLOCK,
        [109] = SDL_SCANCODE_F10, [110] = SDL_SCANCODE_APPLICATION, [111] = SDL_SCANCODE_F12,
        [113] = SDL_SCANCODE_PAUSE, [114] = SDL_SCANCODE_INSERT, [115] = SDL_SCANCODE_HOME,
        [116] = SDL_SCANCODE_PAGEUP, [117] = SDL_SCANCODE_DELETE, [118] = SDL_SCANCODE_F4,
        [119] = SDL_SCANCODE_END, [120] = SDL_SCANCODE_F2, [121] = SDL_SCANCODE_PAGEDOWN,
        [122] = SDL_SCANCODE_F1, [123] = SDL_SCANCODE_LEFT, [124] = SDL_SCANCODE_RIGHT,
        [125] = SDL_SCANCODE_DOWN, [126] = SDL_SCANCODE_UP, [127] = SDL_SCANCODE_POWER,
    };
    return code < sizeof(keys) / sizeof(keys[0]) ? keys[code] : SDL_SCANCODE_UNKNOWN;
}

@interface Fe8SettingsController : NSObject <NSWindowDelegate>
@property(nonatomic, assign) Fe8HostSettings *settings;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSMutableArray<NSButton *> *bindingButtons;
@property(nonatomic, strong) NSButton *listeningButton;
@property(nonatomic, strong) NSButton *extensionsButton;
@property(nonatomic, strong) NSButton *voxelButton;
@property(nonatomic, strong) NSPopUpButton *voxelBackendPopup;
@property(nonatomic, strong) NSScrollView *settingsScroll;
@property(nonatomic, assign) BOOL lastStoredVoxelGpu;
@property(nonatomic, strong) NSTextField *zoomSensitivityValue;
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
            if (!self.listeningButton || event.window != self.window) return event;
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
                /* Frontend hotkeys are actions, not GBA buttons. Let one key
                 * own one action so an older saved binding cannot silently
                 * consume a newer one earlier in the event loop. */
                for (int other = 0; other < FE8_HOST_HOTKEY_COUNT; ++other) {
                    if (other == hotkey || self.settings->hotkeys[other] != scancode)
                        continue;
                    self.settings->hotkeys[other] = SDL_SCANCODE_UNKNOWN;
                    [NSUserDefaults.standardUserDefaults setInteger:SDL_SCANCODE_UNKNOWN
                        forKey:hotkeyBindingKey((enum Fe8HostHotkey)other)];
                }
                self.settings->hotkeys[hotkey] = scancode;
                ++self.settings->revision;
                [NSUserDefaults.standardUserDefaults setInteger:scancode
                    forKey:hotkeyBindingKey(hotkey)];
                [self stopListening];
                for (NSButton *button in self.bindingButtons)
                    button.title = [self titleForBindingTag:button.tag];
            }
            if (scancode == SDL_SCANCODE_UNKNOWN && event.type == NSEventTypeKeyDown)
                self.listeningButton.title = @"Key not supported — try another";
            return nil;
        }];
}

- (void)settingChanged:(NSButton *)sender {
    int enabled = sender.state == NSControlStateValueOn;
    switch (sender.tag) {
    case 0: self.settings->audio_enabled = enabled; break;
    case 1: self.settings->vsync_enabled = enabled; break;
    case 2: self.settings->extensions_enabled = enabled; break;
    case 3: self.settings->mouse_enabled = enabled; break;
    case 4: fe8_macos_toggle_voxel(self.settings); return;
    default: return;
    }
    ++self.settings->revision;
    NSString *key = sender.tag == 0 ? kAudioKey :
        (sender.tag == 1 ? kVSyncKey :
        (sender.tag == 2 ? kExtensionsKey : kMouseKey));
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:key];
}

- (void)toggleExtensions:(id)sender {
    (void)sender;
    fe8_macos_toggle_extensions(self.settings);
}

- (void)toggleVoxel:(id)sender {
    (void)sender;
    fe8_macos_toggle_voxel(self.settings);
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(toggleVoxel:))
        item.state = self.settings->voxel_enabled ?
            NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(selectVoxelBackend:))
        item.state = (self.settings->voxel_gpu != 0) == (item.tag == 0) ?
            NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(toggleExtensions:))
        item.state = self.settings->extensions_enabled ?
            NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

- (void)setVoxelGpu:(BOOL)gpu {
    self.settings->voxel_gpu = gpu;
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setBool:gpu forKey:kVoxelGpuKey];
    self.lastStoredVoxelGpu = gpu;
    [self.voxelBackendPopup selectItemAtIndex:gpu ? 0 : 1];
}

- (void)voxelBackendChanged:(NSPopUpButton *)sender {
    [self setVoxelGpu:sender.indexOfSelectedItem == 0];
}

- (void)selectVoxelBackend:(NSMenuItem *)sender {
    [self setVoxelGpu:sender.tag == 0];
}

- (void)shaderChanged:(NSPopUpButton *)sender {
    NSInteger shader = sender.indexOfSelectedItem;
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        return;
    self.settings->shader = (enum Fe8HostShader)shader;
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:shader forKey:kShaderKey];
}

- (void)speedupRateChanged:(NSPopUpButton *)sender {
    NSInteger rate = sender.indexOfSelectedItem;
    if (rate < 0 || rate >= FE8_HOST_SPEEDUP_COUNT)
        return;
    self.settings->speedup_rate = (enum Fe8HostSpeedupRate)rate;
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:rate forKey:kSpeedupRateKey];
}

- (NSString *)zoomSensitivityTitle:(double)sensitivity {
    NSString *level = sensitivity <= 0.006 ? @"Low" :
        (sensitivity <= 0.018 ? @"Medium" : @"High");
    return [NSString stringWithFormat:@"%@ · %.1f%%", level, sensitivity * 100.0];
}

- (void)zoomSensitivityChanged:(NSSlider *)sender {
    double sensitivity = fe8_host_clamp_zoom_sensitivity(
        sender.doubleValue / 100.0);
    self.settings->zoom_sensitivity = sensitivity;
    self.zoomSensitivityValue.stringValue =
        [self zoomSensitivityTitle:sensitivity];
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setDouble:sensitivity
        forKey:kZoomSensitivityKey];
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
    self.lastStoredVoxelGpu = [NSUserDefaults.standardUserDefaults boolForKey:kVoxelGpuKey];
    self.stateContext = stateContext;
    self.saveState = saveState;
    self.loadState = loadState;
    self.quickStatePath = quickStatePath;
    self.bindingButtons = [NSMutableArray array];
    CGFloat height = MIN(760, NSScreen.mainScreen.visibleFrame.size.height - 90);
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 450, MAX(400, height))
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"FE8 Frontend Settings";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;

    self.window.minSize = NSMakeSize(450, 400);
    NSView *root = self.window.contentView;
    const CGFloat rendererHeight = 144;
    NSView *renderer = [[NSView alloc] initWithFrame:
        NSMakeRect(0, NSHeight(root.bounds) - rendererHeight, NSWidth(root.bounds), rendererHeight)];
    renderer.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [root addSubview:renderer];
    NSTextField *rendererHeading = [NSTextField labelWithString:@"Voxel rendering"];
    rendererHeading.frame = NSMakeRect(24, 110, 380, 24);
    rendererHeading.font = [NSFont boldSystemFontOfSize:13];
    [renderer addSubview:rendererHeading];
    self.voxelButton = [NSButton checkboxWithTitle:@"Enable voxel renderer (experimental)"
        target:self action:@selector(settingChanged:)];
    self.voxelButton.frame = NSMakeRect(24, 78, 390, 24);
    self.voxelButton.tag = 4;
    self.voxelButton.state = settings->voxel_enabled ? NSControlStateValueOn : NSControlStateValueOff;
    [renderer addSubview:self.voxelButton];
    NSTextField *rendererHint = [NSTextField labelWithString:@"The game window title reports the active backend."];
    rendererHint.frame = NSMakeRect(24, 8, 410, 20);
    rendererHint.font = [NSFont systemFontOfSize:11];
    rendererHint.textColor = NSColor.secondaryLabelColor;
    [renderer addSubview:rendererHint];
    NSBox *divider = [[NSBox alloc] initWithFrame:NSMakeRect(16, 0, 418, 1)];
    divider.boxType = NSBoxSeparator;
    divider.autoresizingMask = NSViewWidthSizable;
    [renderer addSubview:divider];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:
        NSMakeRect(0, 0, NSWidth(root.bounds), NSHeight(root.bounds) - rendererHeight)];
    self.settingsScroll = scroll;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    NSView *content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 430, 790)];
    scroll.documentView = content;
    [self.window.contentView addSubview:scroll];
    NSArray<NSString *> *optionNames = @[
        @"Enable audio", @"Synchronize presentation (VSync)",
        @"Enable extended renderer", @"Enable mouse controls"
    ];
    int optionValues[] = {
        settings->audio_enabled, settings->vsync_enabled,
        settings->extensions_enabled, settings->mouse_enabled
    };
    NSInteger i;
    for (i = 0; i < (NSInteger)optionNames.count; ++i) {
        NSButton *check = [[NSButton alloc]
            initWithFrame:NSMakeRect(24, 735 - i * 30, 380, 24)];
        check.buttonType = NSButtonTypeSwitch;
        check.title = optionNames[i];
        check.state = optionValues[i] ? NSControlStateValueOn : NSControlStateValueOff;
        check.tag = i;
        check.target = self;
        check.action = @selector(settingChanged:);
        if (i == 2)
            self.extensionsButton = check;
        [content addSubview:check];
    }

    NSTextField *voxelLabel = [NSTextField labelWithString:@"Backend"];
    voxelLabel.frame = NSMakeRect(30, 41, 100, 24);
    [renderer addSubview:voxelLabel];
    self.voxelBackendPopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(130, 37, 280, 28) pullsDown:NO];
    [self.voxelBackendPopup addItemsWithTitles:@[@"OpenGL (GPU)", @"Software (CPU)"]];
    [self.voxelBackendPopup selectItemAtIndex:settings->voxel_gpu ? 0 : 1];
    self.voxelBackendPopup.target = self;
    self.voxelBackendPopup.action = @selector(voxelBackendChanged:);
    self.voxelBackendPopup.toolTip = @"OpenGL renders the 3D geometry on the GPU. Software is the compatibility path. The window title reports the active backend.";
    [renderer addSubview:self.voxelBackendPopup];

    NSTextField *shaderLabel = [NSTextField labelWithString:@"Video shader"];
    shaderLabel.frame = NSMakeRect(30, 619, 105, 24);
    [content addSubview:shaderLabel];
    NSPopUpButton *shaderPopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(150, 615, 245, 28) pullsDown:NO];
    for (i = 0; i < FE8_HOST_SHADER_COUNT; ++i)
        [shaderPopup addItemWithTitle:[NSString stringWithUTF8String:
            fe8_host_shader_name((enum Fe8HostShader)i)]];
    [shaderPopup selectItemAtIndex:settings->shader];
    shaderPopup.target = self;
    shaderPopup.action = @selector(shaderChanged:);
    [content addSubview:shaderPopup];

    NSTextField *zoomLabel = [NSTextField labelWithString:@"Zoom sensitivity"];
    zoomLabel.frame = NSMakeRect(30, 579, 115, 24);
    [content addSubview:zoomLabel];
    NSSlider *zoomSlider = [NSSlider sliderWithValue:
        settings->zoom_sensitivity * 100.0
        minValue:FE8_HOST_ZOOM_SENSITIVITY_LOW * 100.0
        maxValue:FE8_HOST_ZOOM_SENSITIVITY_HIGH * 100.0
        target:self action:@selector(zoomSensitivityChanged:)];
    zoomSlider.frame = NSMakeRect(150, 579, 150, 24);
    zoomSlider.continuous = YES;
    zoomSlider.numberOfTickMarks = 6;
    zoomSlider.allowsTickMarkValuesOnly = NO;
    [content addSubview:zoomSlider];
    self.zoomSensitivityValue = [NSTextField labelWithString:
        [self zoomSensitivityTitle:settings->zoom_sensitivity]];
    self.zoomSensitivityValue.frame = NSMakeRect(307, 579, 95, 24);
    self.zoomSensitivityValue.alignment = NSTextAlignmentRight;
    [content addSubview:self.zoomSensitivityValue];

    NSTextField *speedupLabel = [NSTextField labelWithString:@"Speed-up rate"];
    speedupLabel.frame = NSMakeRect(30, 539, 115, 24);
    [content addSubview:speedupLabel];
    NSPopUpButton *speedupPopup = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(150, 535, 245, 28) pullsDown:NO];
    for (i = 0; i < FE8_HOST_SPEEDUP_COUNT; ++i)
        [speedupPopup addItemWithTitle:[NSString stringWithUTF8String:
            fe8_host_speedup_name((enum Fe8HostSpeedupRate)i)]];
    [speedupPopup selectItemAtIndex:settings->speedup_rate];
    speedupPopup.target = self;
    speedupPopup.action = @selector(speedupRateChanged:);
    [content addSubview:speedupPopup];

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
    hotkeyHeading.frame = NSMakeRect(24, 150, 380, 24);
    hotkeyHeading.font = [NSFont boldSystemFontOfSize:13];
    [content addSubview:hotkeyHeading];
    for (i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) {
        /* Five rows fit fully inside the fixed settings window. The previous
         * 29-point spacing put the Voxel Renderer button at y=-8. */
        CGFloat y = 118 - i * 27;
        NSInteger tag = FE8_HOTKEY_TAG_BASE + i;
        NSTextField *label = [NSTextField labelWithString:[NSString stringWithUTF8String:
            fe8_host_hotkey_name((enum Fe8HostHotkey)i)]];
        label.frame = NSMakeRect(30, y + 4, 145, 20);
        [content addSubview:label];
        NSButton *binding = [NSButton buttonWithTitle:[self titleForBindingTag:tag]
            target:self action:@selector(captureBinding:)];
        binding.frame = NSMakeRect(180, y, 215, 26);
        binding.tag = tag;
        binding.bezelStyle = NSBezelStyleRounded;
        [self.bindingButtons addObject:binding];
        [content addSubview:binding];
    }
    [content scrollPoint:NSMakePoint(0, NSHeight(content.bounds))];
    [self.window center];
    return self;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    [self stopListening];
}

- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    [self stopListening];
}

- (void)refreshBindings:(NSNotification *)notification {
    (void)notification;
    /* Library and game are different processes. Pick up bindings saved in the
     * other window on activation; don't overwrite session-only CLI overrides. */
    Fe8HostSettings stored = *self.settings;
    fe8_macos_load_settings(&stored);
    /* A real preference change in the Library applies to the running game.
     * Mere activation must not undo --voxel-software or a GPU failure fallback. */
    if ((stored.voxel_gpu != 0) != self.lastStoredVoxelGpu) {
        self.settings->voxel_gpu = stored.voxel_gpu;
        self.lastStoredVoxelGpu = stored.voxel_gpu != 0;
        ++self.settings->revision;
    }
    [self.voxelBackendPopup selectItemAtIndex:self.settings->voxel_gpu ? 0 : 1];
    if (memcmp(stored.bindings, self.settings->bindings, sizeof(stored.bindings)) ||
            memcmp(stored.hotkeys, self.settings->hotkeys, sizeof(stored.hotkeys))) {
        memcpy(self.settings->bindings, stored.bindings, sizeof(stored.bindings));
        memcpy(self.settings->hotkeys, stored.hotkeys, sizeof(stored.hotkeys));
        ++self.settings->revision;
        for (NSButton *button in self.bindingButtons)
            if (button != self.listeningButton)
                button.title = [self titleForBindingTag:button.tag];
    }
}

- (void)showSettings:(id)sender {
    (void)sender;
    [self refreshBindings:nil];
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

void fe8_macos_toggle_extensions(Fe8HostSettings *settings) {
    if (!settings)
        return;
    fe8_host_toggle_extensions(settings);
    [NSUserDefaults.standardUserDefaults setBool:settings->extensions_enabled
        forKey:kExtensionsKey];
    if (settingsController.settings == settings)
        settingsController.extensionsButton.state = settings->extensions_enabled ?
            NSControlStateValueOn : NSControlStateValueOff;
}

void fe8_macos_toggle_voxel(Fe8HostSettings *settings) {
    if (!settings) return;
    fe8_host_toggle_voxel(settings);
    [NSUserDefaults.standardUserDefaults setBool:settings->voxel_enabled forKey:kVoxelKey];
    if (settingsController.settings == settings)
        settingsController.voxelButton.state = settings->voxel_enabled ?
            NSControlStateValueOn : NSControlStateValueOff;
}

void fe8_macos_load_settings(Fe8HostSettings *settings) {
    @autoreleasepool {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        [defaults registerDefaults:@{
            kAudioKey: @YES,
            kVSyncKey: @YES,
            kExtensionsKey: @YES,
            kVoxelKey: @NO,
            kVoxelGpuKey: @YES,
            kMouseKey: @YES,
            kShaderKey: @(FE8_HOST_SHADER_OFF),
            kZoomSensitivityKey: @(FE8_HOST_ZOOM_SENSITIVITY_LOW),
            kSpeedupRateKey: @(FE8_HOST_SPEEDUP_4X),
        }];
        settings->audio_enabled = [defaults boolForKey:kAudioKey];
        settings->vsync_enabled = [defaults boolForKey:kVSyncKey];
        settings->extensions_enabled = [defaults boolForKey:kExtensionsKey];
        settings->mouse_enabled = [defaults boolForKey:kMouseKey];
        settings->voxel_enabled = [defaults boolForKey:kVoxelKey];
        settings->voxel_gpu = [defaults boolForKey:kVoxelGpuKey];
        NSInteger shader = [defaults integerForKey:kShaderKey];
        settings->shader = shader >= 0 && shader < FE8_HOST_SHADER_COUNT ?
            (enum Fe8HostShader)shader : FE8_HOST_SHADER_OFF;
        settings->zoom_sensitivity = fe8_host_clamp_zoom_sensitivity(
            [defaults doubleForKey:kZoomSensitivityKey]);
        NSInteger speedupRate = [defaults integerForKey:kSpeedupRateKey];
        settings->speedup_rate =
            speedupRate >= 0 && speedupRate < FE8_HOST_SPEEDUP_COUNT ?
            (enum Fe8HostSpeedupRate)speedupRate : FE8_HOST_SPEEDUP_4X;
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
        [NSNotificationCenter.defaultCenter addObserver:settingsController
            selector:@selector(refreshBindings:) name:NSApplicationDidBecomeActiveNotification
            object:NSApp];
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
        [settingsMenu addItem:stateMenuItem(@"Extended Renderer",
            @selector(toggleExtensions:), @"", 0)];
        [settingsMenu addItem:stateMenuItem(@"Voxel Renderer", @selector(toggleVoxel:), @"", 0)];
        NSMenuItem *backendRoot = [[NSMenuItem alloc] initWithTitle:@"Voxel Backend" action:nil keyEquivalent:@""];
        NSMenu *backend = [[NSMenu alloc] initWithTitle:@"Voxel Backend"];
        NSArray<NSString *> *backendNames = @[@"OpenGL (GPU)", @"Software (CPU)"];
        for (NSInteger i = 0; i < (NSInteger)backendNames.count; ++i) {
            NSMenuItem *item = stateMenuItem(backendNames[i], @selector(selectVoxelBackend:), @"", 0);
            item.tag = i;
            [backend addItem:item];
        }
        backendRoot.submenu = backend;
        [settingsMenu addItem:backendRoot];
        [settingsMenu addItem:NSMenuItem.separatorItem];
        [settingsMenu addItem:open];
        settingsRoot.submenu = settingsMenu;
        [mainMenu addItem:settingsRoot];
    }
}
