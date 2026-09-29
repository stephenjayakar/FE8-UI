#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_settings.h"

static NSString *const kAudioKey = @"FE8AudioEnabled";
static NSString *const kVSyncKey = @"FE8VSyncEnabled";
static NSString *const kExtensionsKey = @"FE8ExtensionsEnabled";
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

void fe8_macos_show_shader_settings(void);
void fe8_macos_install_shader_menu(NSMenu *menu);

/* Toolbar-style preference tabs that resize the window to each pane. */
@interface Fe8SettingsTabController : NSTabViewController
@end

@implementation Fe8SettingsTabController

- (void)fitWindowToItem:(NSTabViewItem *)item animate:(BOOL)animate {
    NSWindow *window = self.view.window;
    NSView *pane = item.viewController.view;
    if (!window || !pane)
        return;
    NSSize size = item.viewController.preferredContentSize;
    NSRect content = [window contentRectForFrameRect:window.frame];
    NSRect target = [window frameRectForContentRect:
        NSMakeRect(NSMinX(content), NSMaxY(content) - size.height, size.width, size.height)];
    [window setFrame:target display:YES animate:animate && window.visible];
}

- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item {
    [super tabView:tabView didSelectTabViewItem:item];
    [self fitWindowToItem:item animate:YES];
}

@end

@interface Fe8SettingsController : NSObject <NSWindowDelegate>
@property(nonatomic, assign) Fe8HostSettings *settings;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) Fe8SettingsTabController *tabs;
@property(nonatomic, strong) NSMutableArray<NSButton *> *bindingButtons;
@property(nonatomic, strong) NSButton *listeningButton;
@property(nonatomic, strong) NSButton *audioButton;
@property(nonatomic, strong) NSButton *vsyncButton;
@property(nonatomic, strong) NSButton *mouseButton;
@property(nonatomic, strong) NSButton *extensionsButton;
@property(nonatomic, strong) NSPopUpButton *shaderPopup;
@property(nonatomic, strong) NSPopUpButton *speedupPopup;
@property(nonatomic, strong) NSSlider *zoomSlider;
@property(nonatomic, strong) NSTextField *zoomSensitivityValue;
@property(nonatomic, strong) NSTextField *bindingStatus;
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

static const CGFloat kPaneWidth = 540;

static NSTextField *formLabel(NSString *text) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.alignment = NSTextAlignmentRight;
    return label;
}

static NSTextField *hintLabel(NSString *text) {
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    label.textColor = NSColor.secondaryLabelColor;
    label.preferredMaxLayoutWidth = 330;
    label.selectable = NO;
    return label;
}

static NSGridView *formGrid(void) {
    NSGridView *grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    grid.rowSpacing = 10;
    grid.columnSpacing = 10;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    return grid;
}

static void finishFormGrid(NSGridView *grid) {
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:0].width = 150;
    [grid columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;
}

static void addFormRow(NSGridView *grid, NSString *label, NSView *control) {
    [grid addRowWithViews:@[label ? formLabel(label) : NSGridCell.emptyContentView, control]];
}

static void addHintRow(NSGridView *grid, NSString *text) {
    NSGridRow *row = [grid addRowWithViews:@[NSGridCell.emptyContentView, hintLabel(text)]];
    row.topPadding = -5;
}

static void addSeparatorRow(NSGridView *grid) {
    NSBox *line = [[NSBox alloc] init];
    line.boxType = NSBoxSeparator;
    NSGridRow *row = [grid addRowWithViews:@[line]];
    [row mergeCellsInRange:NSMakeRange(0, 2)];
    row.topPadding = 4;
    row.bottomPadding = 4;
    [row cellAtIndex:0].xPlacement = NSGridCellPlacementFill;
}

static NSViewController *paneController(NSString *title, NSView *body) {
    NSView *pane = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kPaneWidth, 200)];
    body.translatesAutoresizingMaskIntoConstraints = NO;
    [body setContentHuggingPriority:NSLayoutPriorityRequired
        forOrientation:NSLayoutConstraintOrientationVertical];
    [pane addSubview:body];
    NSLayoutConstraint *bottom = [body.bottomAnchor
        constraintEqualToAnchor:pane.bottomAnchor constant:-24];
    [NSLayoutConstraint activateConstraints:@[
        [pane.widthAnchor constraintEqualToConstant:kPaneWidth],
        [body.topAnchor constraintEqualToAnchor:pane.topAnchor constant:22],
        bottom,
        [body.centerXAnchor constraintEqualToAnchor:pane.centerXAnchor],
        [body.leadingAnchor constraintGreaterThanOrEqualToAnchor:pane.leadingAnchor constant:20],
    ]];
    /* Freeze each pane at its natural height so a taller sibling tab can
     * never stretch the grid rows apart. */
    NSSize size = pane.fittingSize;
    bottom.active = NO;
    [body.bottomAnchor constraintLessThanOrEqualToAnchor:pane.bottomAnchor constant:-24].active = YES;
    [pane.heightAnchor constraintEqualToConstant:size.height].active = YES;
    NSViewController *controller = [[NSViewController alloc] init];
    controller.view = pane;
    controller.title = title;
    controller.preferredContentSize = size;
    return controller;
}

@implementation Fe8SettingsController

- (NSString *)titleForBindingTag:(NSInteger)tag {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (tag >= 0 && tag < FE8_HOST_BUTTON_COUNT)
        scancode = self.settings->bindings[tag];
    else if (tag >= FE8_HOTKEY_TAG_BASE &&
            tag < FE8_HOTKEY_TAG_BASE + FE8_HOST_HOTKEY_COUNT)
        scancode = self.settings->hotkeys[tag - FE8_HOTKEY_TAG_BASE];
    const char *name = SDL_GetScancodeName(scancode);
    return name && *name ? [NSString stringWithUTF8String:name] : @"Not Set";
}

- (void)setBindingButton:(NSButton *)button listening:(BOOL)listening {
    button.bezelColor = listening ? NSColor.controlAccentColor : nil;
    button.state = listening ? NSControlStateValueOn : NSControlStateValueOff;
    button.title = listening ? @"Press a key…" : [self titleForBindingTag:button.tag];
    button.font = listening ? [NSFont systemFontOfSize:NSFont.systemFontSize] :
        [NSFont monospacedSystemFontOfSize:NSFont.systemFontSize weight:NSFontWeightMedium];
    BOOL unbound = !listening && [button.title isEqualToString:@"Not Set"];
    button.contentTintColor = unbound ? NSColor.tertiaryLabelColor : nil;
}

- (void)setBindingStatusText:(NSString *)text warning:(BOOL)warning {
    self.bindingStatus.stringValue = text;
    self.bindingStatus.textColor = warning ? NSColor.systemOrangeColor :
        NSColor.secondaryLabelColor;
}

- (void)resetBindingStatus {
    [self setBindingStatusText:@"Click a binding, then press the key to use. Esc cancels."
        warning:NO];
}

- (void)stopListening {
    if (self.keyMonitor) {
        [NSEvent removeMonitor:self.keyMonitor];
        self.keyMonitor = nil;
    }
    if (self.listeningButton) {
        NSButton *button = self.listeningButton;
        self.listeningButton = nil;
        [self setBindingButton:button listening:NO];
    }
    [self resetBindingStatus];
}

- (void)refreshBindingButtons {
    for (NSButton *button in self.bindingButtons)
        if (button != self.listeningButton)
            [self setBindingButton:button listening:NO];
}

- (void)captureBinding:(NSButton *)sender {
    BOOL toggleOff = self.listeningButton == sender;
    [self stopListening];
    if (toggleOff)
        return;
    self.listeningButton = sender;
    [self setBindingButton:sender listening:YES];
    [self setBindingStatusText:@"Press the new key now. Esc cancels." warning:NO];
    [self.window makeFirstResponder:nil];
    self.keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskKeyDown | NSEventMaskFlagsChanged
        handler:^NSEvent *(NSEvent *event) {
            if (!self.listeningButton) return event;
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
            if (scancode == SDL_SCANCODE_UNKNOWN && event.type == NSEventTypeKeyDown)
                [self setBindingStatusText:@"That key isn't supported. Try another, or press Esc."
                    warning:YES];
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

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(toggleExtensions:))
        item.state = self.settings->extensions_enabled ?
            NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

- (void)shaderChanged:(NSPopUpButton *)sender {
    NSInteger shader = sender.indexOfSelectedItem;
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        return;
    self.settings->shader = (enum Fe8HostShader)shader;
    ++self.settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:shader forKey:kShaderKey];
}

- (void)showShaderSettings:(id)sender {
    (void)sender;
    fe8_macos_show_shader_settings();
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
    return [NSString stringWithFormat:@"%@ (%.1f%%)", level, sensitivity * 100.0];
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

#pragma mark Construction

- (NSButton *)checkbox:(NSString *)title tag:(NSInteger)tag on:(int)on {
    NSButton *check = [NSButton checkboxWithTitle:title target:self
        action:@selector(settingChanged:)];
    check.tag = tag;
    check.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    return check;
}

- (NSPopUpButton *)popupWithTitles:(NSArray<NSString *> *)titles selected:(NSInteger)selected
    action:(SEL)action {
    NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [popup addItemsWithTitles:titles];
    [popup selectItemAtIndex:selected];
    popup.target = self;
    popup.action = action;
    [popup.widthAnchor constraintGreaterThanOrEqualToConstant:190].active = YES;
    return popup;
}

- (NSButton *)bindingButtonWithTag:(NSInteger)tag {
    NSButton *binding = [NSButton buttonWithTitle:@"" target:self
        action:@selector(captureBinding:)];
    binding.tag = tag;
    binding.bezelStyle = NSBezelStyleRounded;
    [binding.widthAnchor constraintEqualToConstant:150].active = YES;
    [self setBindingButton:binding listening:NO];
    [self.bindingButtons addObject:binding];
    return binding;
}

- (NSViewController *)generalPane {
    Fe8HostSettings *settings = self.settings;
    NSGridView *grid = formGrid();
    self.audioButton = [self checkbox:@"Play game audio" tag:0 on:settings->audio_enabled];
    addFormRow(grid, @"Sound:", self.audioButton);
    self.vsyncButton = [self checkbox:@"Synchronize with display (VSync)" tag:1
        on:settings->vsync_enabled];
    addFormRow(grid, @"Presentation:", self.vsyncButton);
    addHintRow(grid, @"Emulation is always paced at the GBA's native 59.73 fps.");

    addSeparatorRow(grid);
    self.mouseButton = [self checkbox:@"Enable mouse controls" tag:3 on:settings->mouse_enabled];
    addFormRow(grid, @"Mouse:", self.mouseButton);
    addHintRow(grid, @"Point to move the map cursor. Click for A, right-click for B, "
        @"Shift-drag to pan.");

    NSMutableArray<NSString *> *rates = [NSMutableArray array];
    for (NSInteger i = 0; i < FE8_HOST_SPEEDUP_COUNT; ++i)
        [rates addObject:[NSString stringWithUTF8String:
            fe8_host_speedup_name((enum Fe8HostSpeedupRate)i)]];
    self.speedupPopup = [self popupWithTitles:rates selected:settings->speedup_rate
        action:@selector(speedupRateChanged:)];
    addSeparatorRow(grid);
    addFormRow(grid, @"Speed-up rate:", self.speedupPopup);
    addHintRow(grid, @"Applies while the Speed Up hotkey is held.");

    self.zoomSlider = [NSSlider sliderWithValue:settings->zoom_sensitivity * 100.0
        minValue:FE8_HOST_ZOOM_SENSITIVITY_LOW * 100.0
        maxValue:FE8_HOST_ZOOM_SENSITIVITY_HIGH * 100.0
        target:self action:@selector(zoomSensitivityChanged:)];
    self.zoomSlider.continuous = YES;
    self.zoomSlider.numberOfTickMarks = 6;
    [self.zoomSlider.widthAnchor constraintEqualToConstant:190].active = YES;
    self.zoomSensitivityValue = [NSTextField labelWithString:
        [self zoomSensitivityTitle:settings->zoom_sensitivity]];
    self.zoomSensitivityValue.font = [NSFont monospacedDigitSystemFontOfSize:
        NSFont.smallSystemFontSize weight:NSFontWeightRegular];
    self.zoomSensitivityValue.textColor = NSColor.secondaryLabelColor;
    NSStackView *zoom = [NSStackView stackViewWithViews:@[self.zoomSlider,
        self.zoomSensitivityValue]];
    zoom.spacing = 10;
    zoom.alignment = NSLayoutAttributeFirstBaseline;
    addFormRow(grid, @"Zoom sensitivity:", zoom);
    addHintRow(grid, @"How far each scroll-wheel step zooms the map.");
    finishFormGrid(grid);
    return paneController(@"General", grid);
}

- (NSViewController *)videoPane {
    Fe8HostSettings *settings = self.settings;
    NSGridView *grid = formGrid();
    self.extensionsButton = [self checkbox:@"Extended renderer" tag:2
        on:settings->extensions_enabled];
    addFormRow(grid, @"Map:", self.extensionsButton);
    addHintRow(grid, @"Draws terrain and units beyond the 240×160 GBA frame.");

    addSeparatorRow(grid);
    NSMutableArray<NSString *> *shaders = [NSMutableArray array];
    for (NSInteger i = 0; i < FE8_HOST_SHADER_COUNT; ++i)
        [shaders addObject:[NSString stringWithUTF8String:
            fe8_host_shader_name((enum Fe8HostShader)i)]];
    self.shaderPopup = [self popupWithTitles:shaders selected:settings->shader
        action:@selector(shaderChanged:)];
    NSButton *adjust = [NSButton buttonWithTitle:@"Adjust…" target:self
        action:@selector(showShaderSettings:)];
    NSStackView *shaderRow = [NSStackView stackViewWithViews:@[self.shaderPopup, adjust]];
    shaderRow.spacing = 8;
    shaderRow.alignment = NSLayoutAttributeFirstBaseline;
    addFormRow(grid, @"Video shader:", shaderRow);
    addHintRow(grid, @"Applied to the whole canvas, including the extended map.");
    finishFormGrid(grid);
    return paneController(@"Video", grid);
}

- (NSGridView *)bindingGridWithTitle:(NSString *)title buttons:(const enum Fe8HostButton *)buttons
    count:(NSInteger)count {
    NSGridView *grid = formGrid();
    grid.rowSpacing = 8;
    NSTextField *heading = [NSTextField labelWithString:title];
    heading.font = [NSFont systemFontOfSize:NSFont.systemFontSize weight:NSFontWeightSemibold];
    NSGridRow *headingRow = [grid addRowWithViews:@[heading]];
    [headingRow mergeCellsInRange:NSMakeRange(0, 2)];
    [headingRow cellAtIndex:0].xPlacement = NSGridCellPlacementLeading;
    headingRow.bottomPadding = 2;
    for (NSInteger i = 0; i < count; ++i)
        [grid addRowWithViews:@[formLabel([NSString stringWithUTF8String:
            fe8_host_button_name(buttons[i])]), [self bindingButtonWithTag:buttons[i]]]];
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:0].width = 52;
    return grid;
}

- (NSTextField *)statusLabel {
    NSTextField *status = [NSTextField labelWithString:@""];
    status.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    status.alignment = NSTextAlignmentCenter;
    return status;
}

- (NSViewController *)controlsPane {
    static const enum Fe8HostButton face[] = {
        FE8_HOST_A, FE8_HOST_B, FE8_HOST_L, FE8_HOST_R, FE8_HOST_START, FE8_HOST_SELECT,
    };
    static const enum Fe8HostButton dpad[] = {
        FE8_HOST_UP, FE8_HOST_DOWN, FE8_HOST_LEFT, FE8_HOST_RIGHT,
    };
    NSGridView *buttons = [self bindingGridWithTitle:@"Buttons" buttons:face count:6];
    NSGridView *directions = [self bindingGridWithTitle:@"D-Pad" buttons:dpad count:4];
    for (NSGridView *grid in @[buttons, directions])
        [grid setContentHuggingPriority:NSLayoutPriorityRequired
            forOrientation:NSLayoutConstraintOrientationVertical];
    NSStackView *columns = [NSStackView stackViewWithViews:@[buttons, directions]];
    columns.alignment = NSLayoutAttributeTop;
    columns.spacing = 36;

    NSTextField *status = [self statusLabel];
    NSStackView *body = [NSStackView stackViewWithViews:@[columns, status]];
    body.orientation = NSUserInterfaceLayoutOrientationVertical;
    body.spacing = 18;
    self.bindingStatus = status;
    [self resetBindingStatus];
    return paneController(@"Controls", body);
}

- (NSViewController *)hotkeysPane {
    NSGridView *grid = formGrid();
    grid.rowSpacing = 8;
    static NSString *const hints[FE8_HOST_HOTKEY_COUNT] = {
        @"Hold to fast-forward", @"Save the quick state", @"Load the quick state",
        @"Toggle the extended map",
    };
    for (NSInteger i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) {
        NSButton *binding = [self bindingButtonWithTag:FE8_HOTKEY_TAG_BASE + i];
        NSTextField *hint = [NSTextField labelWithString:hints[i]];
        hint.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
        hint.textColor = NSColor.secondaryLabelColor;
        NSStackView *row = [NSStackView stackViewWithViews:@[binding, hint]];
        row.spacing = 10;
        row.alignment = NSLayoutAttributeFirstBaseline;
        addFormRow(grid, [NSString stringWithFormat:@"%s:",
            fe8_host_hotkey_name((enum Fe8HostHotkey)i)], row);
    }
    finishFormGrid(grid);
    [grid columnAtIndex:0].width = 130;

    NSTextField *status = [self statusLabel];
    status.stringValue = @"Click a binding, then press the key to use. Esc cancels.";
    status.textColor = NSColor.secondaryLabelColor;
    NSStackView *body = [NSStackView stackViewWithViews:@[grid, status]];
    body.orientation = NSUserInterfaceLayoutOrientationVertical;
    body.spacing = 18;
    return paneController(@"Hotkeys", body);
}

- (NSTabViewItem *)tabItem:(NSViewController *)controller symbol:(NSString *)symbolName {
    NSTabViewItem *item = [NSTabViewItem tabViewItemWithViewController:controller];
    item.label = controller.title;
    item.image = [NSImage imageWithSystemSymbolName:symbolName
        accessibilityDescription:controller.title];
    return item;
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

    self.tabs = [[Fe8SettingsTabController alloc] init];
    self.tabs.tabStyle = NSTabViewControllerTabStyleToolbar;
    self.tabs.transitionOptions = NSViewControllerTransitionNone;
    [self.tabs addTabViewItem:[self tabItem:[self generalPane] symbol:@"gearshape"]];
    [self.tabs addTabViewItem:[self tabItem:[self videoPane] symbol:@"display"]];
    [self.tabs addTabViewItem:[self tabItem:[self controlsPane] symbol:@"gamecontroller"]];
    [self.tabs addTabViewItem:[self tabItem:[self hotkeysPane] symbol:@"keyboard"]];

    self.window = [NSWindow windowWithContentViewController:self.tabs];
    self.window.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    self.window.toolbarStyle = NSWindowToolbarStylePreference;
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    [self.tabs fitWindowToItem:self.tabs.tabViewItems[self.tabs.selectedTabViewItemIndex]
        animate:NO];
    [self.window center];
    return self;
}

- (void)refreshControls {
    Fe8HostSettings *settings = self.settings;
    self.audioButton.state = settings->audio_enabled;
    self.vsyncButton.state = settings->vsync_enabled;
    self.mouseButton.state = settings->mouse_enabled;
    self.extensionsButton.state = settings->extensions_enabled;
    [self.shaderPopup selectItemAtIndex:settings->shader];
    [self.speedupPopup selectItemAtIndex:settings->speedup_rate];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    (void)notification;
    [self refreshControls];
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

void fe8_macos_load_settings(Fe8HostSettings *settings) {
    @autoreleasepool {
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        [defaults registerDefaults:@{
            kAudioKey: @YES,
            kVSyncKey: @YES,
            kExtensionsKey: @YES,
            kMouseKey: @YES,
            kShaderKey: @(FE8_HOST_SHADER_OFF),
            kZoomSensitivityKey: @(FE8_HOST_ZOOM_SENSITIVITY_LOW),
            kSpeedupRateKey: @(FE8_HOST_SPEEDUP_4X),
        }];
        settings->audio_enabled = [defaults boolForKey:kAudioKey];
        settings->vsync_enabled = [defaults boolForKey:kVSyncKey];
        settings->extensions_enabled = [defaults boolForKey:kExtensionsKey];
        settings->mouse_enabled = [defaults boolForKey:kMouseKey];
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
        NSWindow.allowsAutomaticWindowTabbing = NO;
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

        /* macOS convention: Settings… (⌘,) lives in the application menu.
         * SDL already reserves a disabled "Preferences…" slot; reuse it. */
        NSMenu *appMenu = mainMenu.numberOfItems ? [mainMenu itemAtIndex:0].submenu : nil;
        NSMenuItem *open = nil;
        for (NSMenuItem *item in appMenu.itemArray)
            if ([item.keyEquivalent isEqualToString:@","])
                open = item;
        if (!open && appMenu) {
            NSInteger index = appMenu.numberOfItems > 0 &&
                [appMenu itemAtIndex:0].action == @selector(orderFrontStandardAboutPanel:) ? 1 : 0;
            if (index == 1)
                [appMenu insertItem:NSMenuItem.separatorItem atIndex:index++];
            open = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@","];
            [appMenu insertItem:open atIndex:index];
        }
        open.title = @"Settings…";
        open.action = @selector(showSettings:);
        open.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        open.target = settingsController;

        NSInteger windowIndex = NSApp.windowsMenu ?
            [mainMenu indexOfItemWithSubmenu:NSApp.windowsMenu] : -1;
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
            if (windowIndex >= 0)
                [mainMenu insertItem:stateRoot atIndex:windowIndex++];
            else
                [mainMenu addItem:stateRoot];
        }

        NSMenuItem *viewRoot = [[NSMenuItem alloc]
            initWithTitle:@"View" action:nil keyEquivalent:@""];
        NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        [viewMenu addItem:stateMenuItem(@"Extended Renderer",
            @selector(toggleExtensions:), @"", 0)];
        [viewMenu addItem:NSMenuItem.separatorItem];
        fe8_macos_install_shader_menu(viewMenu);
        viewRoot.submenu = viewMenu;
        if (windowIndex >= 0)
            [mainMenu insertItem:viewRoot atIndex:windowIndex];
        else
            [mainMenu addItem:viewRoot];
    }
}
