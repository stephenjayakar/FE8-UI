#import <Cocoa/Cocoa.h>

#include "host_settings.h"

static NSString *const kShaderModeKey = @"FE8ShaderMode";
static NSString *const kShaderParameterPrefix = @"FE8ShaderParameter";

@interface Fe8ShaderMenuController : NSObject <NSMenuDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSMenu *menu;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextField *presetLabel;
@property(nonatomic, strong) NSPopUpButton *presetPopup;
@property(nonatomic, strong) NSButton *resetButton;
@property(nonatomic, strong) NSMutableArray<NSSlider *> *sliders;
@property(nonatomic, strong) NSMutableArray<NSTextField *> *valueLabels;
@end

@implementation Fe8ShaderMenuController

- (NSString *)keyForShader:(enum Fe8HostShader)shader parameter:(NSInteger)parameter {
    return [NSString stringWithFormat:@"%@.%ld.%ld", kShaderParameterPrefix,
        (long)shader, (long)parameter];
}

- (double)valueForConfig:(const Fe8HostShaderConfig *)config parameter:(NSInteger)parameter {
    switch (parameter) {
    case 0: return config->scanline_strength;
    case 1: return config->mask_strength;
    case 2: return config->blur;
    case 3: return config->bloom;
    case 4: return config->curvature;
    case 5: return config->saturation;
    default: return 0.0;
    }
}

- (void)setValue:(double)value config:(Fe8HostShaderConfig *)config parameter:(NSInteger)parameter {
    switch (parameter) {
    case 0: config->scanline_strength = (float)value; break;
    case 1: config->mask_strength = (float)value; break;
    case 2: config->blur = (float)value; break;
    case 3: config->bloom = (float)value; break;
    case 4: config->curvature = (float)value; break;
    case 5: config->saturation = (float)value; break;
    default: break;
    }
}

- (double)minimumForParameter:(NSInteger)parameter {
    return parameter == 5 ? 0.5 : 0.0;
}

- (double)maximumForParameter:(NSInteger)parameter {
    if (parameter == 3)
        return 0.5;
    if (parameter == 4)
        return 0.25;
    if (parameter == 5)
        return 1.5;
    return 1.0;
}

- (void)persistConfig:(enum Fe8HostShader)shader config:(const Fe8HostShaderConfig *)config {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    for (NSInteger parameter = 0; parameter < 6; ++parameter)
        [defaults setDouble:[self valueForConfig:config parameter:parameter]
            forKey:[self keyForShader:shader parameter:parameter]];
}

- (void)loadPersistedConfigs {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    for (NSInteger shader = 1; shader < FE8_HOST_SHADER_COUNT; ++shader) {
        Fe8HostShaderConfig config;
        fe8_host_shader_get_config((enum Fe8HostShader)shader, &config);
        for (NSInteger parameter = 0; parameter < 6; ++parameter) {
            NSString *key = [self keyForShader:(enum Fe8HostShader)shader parameter:parameter];
            if ([defaults objectForKey:key])
                [self setValue:[defaults doubleForKey:key] config:&config parameter:parameter];
        }
        fe8_host_shader_set_config((enum Fe8HostShader)shader, &config);
    }
}

- (void)selectShader:(NSMenuItem *)sender {
    Fe8HostSettings *settings = fe8_host_settings_current();
    if (!settings || sender.tag < 0 || sender.tag >= FE8_HOST_SHADER_COUNT)
        return;
    settings->shader = (enum Fe8HostShader)sender.tag;
    ++settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:sender.tag forKey:kShaderModeKey];
    [self refreshControls];
}

- (void)parameterChanged:(NSSlider *)sender {
    Fe8HostSettings *settings = fe8_host_settings_current();
    Fe8HostShaderConfig config;
    if (!settings || settings->shader <= FE8_HOST_SHADER_OFF ||
            settings->shader >= FE8_HOST_SHADER_COUNT)
        return;
    fe8_host_shader_get_config(settings->shader, &config);
    [self setValue:sender.doubleValue config:&config parameter:sender.tag];
    fe8_host_shader_set_config(settings->shader, &config);
    [self persistConfig:settings->shader config:&config];
    ++settings->revision;
    [self refreshControls];
}

- (void)resetPreset:(id)sender {
    (void)sender;
    Fe8HostSettings *settings = fe8_host_settings_current();
    Fe8HostShaderConfig config;
    if (!settings || settings->shader <= FE8_HOST_SHADER_OFF ||
            settings->shader >= FE8_HOST_SHADER_COUNT)
        return;
    fe8_host_shader_default_config(settings->shader, &config);
    fe8_host_shader_set_config(settings->shader, &config);
    [self persistConfig:settings->shader config:&config];
    ++settings->revision;
    [self refreshControls];
}

- (void)presetChanged:(NSPopUpButton *)sender {
    Fe8HostSettings *settings = fe8_host_settings_current();
    NSInteger shader = sender.indexOfSelectedItem;
    if (!settings || shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        return;
    settings->shader = (enum Fe8HostShader)shader;
    ++settings->revision;
    [NSUserDefaults.standardUserDefaults setInteger:shader forKey:kShaderModeKey];
    [self refreshControls];
}

- (void)refreshControls {
    Fe8HostSettings *settings = fe8_host_settings_current();
    enum Fe8HostShader shader = settings ? settings->shader : FE8_HOST_SHADER_OFF;
    Fe8HostShaderConfig config;
    fe8_host_shader_get_config(shader, &config);
    [self.presetPopup selectItemAtIndex:shader];
    BOOL adjustable = shader != FE8_HOST_SHADER_OFF;
    self.presetLabel.stringValue = adjustable ?
        @"Changes apply immediately and are remembered for each preset." :
        @"Choose a preset to adjust its parameters.";
    self.resetButton.enabled = adjustable;
    for (NSInteger i = 0; i < (NSInteger)self.sliders.count; ++i) {
        NSSlider *slider = self.sliders[i];
        NSTextField *value = self.valueLabels[i];
        double parameterValue = [self valueForConfig:&config parameter:i];
        slider.enabled = adjustable;
        slider.doubleValue = parameterValue;
        value.stringValue = i == 4 ?
            [NSString stringWithFormat:@"%.3f", parameterValue] :
            [NSString stringWithFormat:@"%.2f", parameterValue];
        value.textColor = adjustable ? NSColor.secondaryLabelColor : NSColor.tertiaryLabelColor;
    }
}

- (void)showShaderSettings:(id)sender {
    (void)sender;
    [self refreshControls];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activate];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    (void)notification;
    [self refreshControls];
}

- (void)menuWillOpen:(NSMenu *)menu {
    Fe8HostSettings *settings = fe8_host_settings_current();
    enum Fe8HostShader shader = settings ? settings->shader : FE8_HOST_SHADER_OFF;
    for (NSMenuItem *item in menu.itemArray) {
        if (item.action == @selector(selectShader:))
            item.state = item.tag == shader ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

- (instancetype)init {
    self = [super init];
    if (!self)
        return nil;
    [self loadPersistedConfigs];
    self.sliders = [NSMutableArray array];
    self.valueLabels = [NSMutableArray array];

    NSGridView *grid = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
    grid.rowSpacing = 12;
    grid.columnSpacing = 10;
    grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    grid.translatesAutoresizingMaskIntoConstraints = NO;

    self.presetPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    for (NSInteger shader = 0; shader < FE8_HOST_SHADER_COUNT; ++shader)
        [self.presetPopup addItemWithTitle:[NSString stringWithUTF8String:
            fe8_host_shader_name((enum Fe8HostShader)shader)]];
    self.presetPopup.target = self;
    self.presetPopup.action = @selector(presetChanged:);
    NSTextField *presetTitle = [NSTextField labelWithString:@"Preset:"];
    NSGridRow *presetRow = [grid addRowWithViews:@[presetTitle, self.presetPopup]];
    [presetRow mergeCellsInRange:NSMakeRange(1, 2)];

    self.presetLabel = [NSTextField wrappingLabelWithString:@""];
    self.presetLabel.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    self.presetLabel.textColor = NSColor.secondaryLabelColor;
    self.presetLabel.preferredMaxLayoutWidth = 300;
    NSGridRow *hintRow = [grid addRowWithViews:@[NSGridCell.emptyContentView, self.presetLabel]];
    [hintRow mergeCellsInRange:NSMakeRange(1, 2)];
    hintRow.topPadding = -6;

    NSBox *line = [[NSBox alloc] init];
    line.boxType = NSBoxSeparator;
    NSGridRow *lineRow = [grid addRowWithViews:@[line]];
    [lineRow mergeCellsInRange:NSMakeRange(0, 3)];
    [lineRow cellAtIndex:0].xPlacement = NSGridCellPlacementFill;
    lineRow.topPadding = 2;
    lineRow.bottomPadding = 2;

    NSArray<NSString *> *names = @[
        @"Scanlines:", @"Mask:", @"Horizontal blur:",
        @"Bloom:", @"Curvature:", @"Saturation:"
    ];
    for (NSInteger i = 0; i < (NSInteger)names.count; ++i) {
        NSTextField *label = [NSTextField labelWithString:names[i]];
        NSSlider *slider = [NSSlider sliderWithValue:0.0
            minValue:[self minimumForParameter:i]
            maxValue:[self maximumForParameter:i]
            target:self action:@selector(parameterChanged:)];
        slider.continuous = YES;
        slider.tag = i;
        [slider.widthAnchor constraintEqualToConstant:220].active = YES;
        [self.sliders addObject:slider];
        NSTextField *value = [NSTextField labelWithString:@"0.00"];
        value.font = [NSFont monospacedDigitSystemFontOfSize:NSFont.smallSystemFontSize
            weight:NSFontWeightRegular];
        value.alignment = NSTextAlignmentRight;
        [value.widthAnchor constraintEqualToConstant:42].active = YES;
        [self.valueLabels addObject:value];
        [grid addRowWithViews:@[label, slider, value]];
    }
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;

    self.resetButton = [NSButton buttonWithTitle:@"Reset to Defaults"
        target:self action:@selector(resetPreset:)];
    self.resetButton.translatesAutoresizingMaskIntoConstraints = NO;

    NSView *content = [[NSView alloc] initWithFrame:NSZeroRect];
    [content addSubview:grid];
    [content addSubview:self.resetButton];
    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:content.topAnchor constant:22],
        [grid.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24],
        [grid.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [self.resetButton.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:20],
        [self.resetButton.trailingAnchor constraintEqualToAnchor:grid.trailingAnchor],
        [self.resetButton.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-20],
    ]];

    NSSize size = content.fittingSize;
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Video Shader";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    self.window.contentView = content;
    [self.window center];
    [self refreshControls];
    return self;
}

- (void)installMenuInto:(NSMenu *)parent {
    NSMenuItem *root = [[NSMenuItem alloc] initWithTitle:@"Video Shader" action:nil
        keyEquivalent:@""];
    self.menu = [[NSMenu alloc] initWithTitle:@"Video Shader"];
    self.menu.delegate = self;
    for (NSInteger shader = 0; shader < FE8_HOST_SHADER_COUNT; ++shader) {
        NSMenuItem *item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:
                fe8_host_shader_name((enum Fe8HostShader)shader)]
            action:@selector(selectShader:) keyEquivalent:@""];
        item.tag = shader;
        item.target = self;
        [self.menu addItem:item];
        if (shader == FE8_HOST_SHADER_OFF)
            [self.menu addItem:NSMenuItem.separatorItem];
    }
    [self.menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *configure = [[NSMenuItem alloc]
        initWithTitle:@"Adjust Shader…" action:@selector(showShaderSettings:)
        keyEquivalent:@""];
    configure.target = self;
    [self.menu addItem:configure];
    root.submenu = self.menu;
    [parent addItem:root];
}

@end

static Fe8ShaderMenuController *shaderMenuController;

static Fe8ShaderMenuController *ensureShaderController(void) {
    if (!shaderMenuController)
        shaderMenuController = [[Fe8ShaderMenuController alloc] init];
    return shaderMenuController;
}

void fe8_macos_install_shader_menu(NSMenu *menu);
void fe8_macos_show_shader_settings(void);

void fe8_macos_install_shader_menu(NSMenu *menu) {
    [ensureShaderController() installMenuInto:menu];
}

void fe8_macos_show_shader_settings(void) {
    [ensureShaderController() showShaderSettings:nil];
}

/* Persisted per-preset parameters must be loaded as soon as AppKit is up,
 * before the first frame is presented; the menu is attached later by
 * fe8_macos_install_settings_menu(). */
__attribute__((constructor))
static void fe8_register_shader_menu(void) {
    @autoreleasepool {
        [NSNotificationCenter.defaultCenter
            addObserverForName:NSApplicationDidFinishLaunchingNotification
            object:nil queue:NSOperationQueue.mainQueue
            usingBlock:^(NSNotification *note) {
                (void)note;
                ensureShaderController();
            }];
    }
}
