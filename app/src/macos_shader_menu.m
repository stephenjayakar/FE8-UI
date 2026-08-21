#import <Cocoa/Cocoa.h>

#include "host_settings.h"

static NSString *const kShaderModeKey = @"FE8ShaderMode";
static NSString *const kShaderParameterPrefix = @"FE8ShaderParameter";

@interface Fe8ShaderMenuController : NSObject <NSMenuDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSMenu *menu;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextField *presetLabel;
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

- (void)refreshControls {
    Fe8HostSettings *settings = fe8_host_settings_current();
    enum Fe8HostShader shader = settings ? settings->shader : FE8_HOST_SHADER_OFF;
    Fe8HostShaderConfig config;
    fe8_host_shader_get_config(shader, &config);
    self.presetLabel.stringValue = [NSString stringWithFormat:@"Preset: %s",
        fe8_host_shader_name(shader)];
    for (NSInteger i = 0; i < self.sliders.count; ++i) {
        NSSlider *slider = self.sliders[i];
        NSTextField *value = self.valueLabels[i];
        double parameterValue = [self valueForConfig:&config parameter:i];
        slider.enabled = shader != FE8_HOST_SHADER_OFF;
        slider.doubleValue = parameterValue;
        value.stringValue = i == 4 ?
            [NSString stringWithFormat:@"%.3f", parameterValue] :
            [NSString stringWithFormat:@"%.2f", parameterValue];
    }
}

- (void)showShaderSettings:(id)sender {
    (void)sender;
    [self refreshControls];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activate];
}

- (void)menuWillOpen:(NSMenu *)menu {
    Fe8HostSettings *settings = fe8_host_settings_current();
    enum Fe8HostShader shader = settings ? settings->shader : FE8_HOST_SHADER_OFF;
    for (NSMenuItem *item in menu.itemArray) {
        if (item.tag >= 0 && item.tag < FE8_HOST_SHADER_COUNT)
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

    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 470, 360)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"CRT Shader Settings";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    NSView *content = self.window.contentView;
    self.presetLabel = [NSTextField labelWithString:@""];
    self.presetLabel.frame = NSMakeRect(24, 315, 420, 24);
    self.presetLabel.font = [NSFont boldSystemFontOfSize:13];
    [content addSubview:self.presetLabel];

    NSArray<NSString *> *names = @[
        @"Scanline strength", @"Mask strength", @"Horizontal blur",
        @"Bloom", @"Curvature", @"Saturation"
    ];
    for (NSInteger i = 0; i < names.count; ++i) {
        CGFloat y = 270 - i * 40;
        NSTextField *label = [NSTextField labelWithString:names[i]];
        label.frame = NSMakeRect(28, y + 3, 125, 20);
        [content addSubview:label];
        NSSlider *slider = [NSSlider sliderWithValue:0.0
            minValue:[self minimumForParameter:i]
            maxValue:[self maximumForParameter:i]
            target:self action:@selector(parameterChanged:)];
        slider.frame = NSMakeRect(155, y, 220, 24);
        slider.continuous = YES;
        slider.tag = i;
        [self.sliders addObject:slider];
        [content addSubview:slider];
        NSTextField *value = [NSTextField labelWithString:@""];
        value.frame = NSMakeRect(382, y + 3, 60, 20);
        value.alignment = NSTextAlignmentRight;
        [self.valueLabels addObject:value];
        [content addSubview:value];
    }
    NSButton *reset = [NSButton buttonWithTitle:@"Reset Preset"
        target:self action:@selector(resetPreset:)];
    reset.frame = NSMakeRect(315, 18, 130, 30);
    [content addSubview:reset];
    [self.window center];
    [self refreshControls];
    return self;
}

- (void)installMenu {
    NSMenu *mainMenu = NSApp.mainMenu;
    if (!mainMenu) {
        mainMenu = [[NSMenu alloc] initWithTitle:@""];
        NSApp.mainMenu = mainMenu;
    }
    NSMenuItem *root = [[NSMenuItem alloc] initWithTitle:@"Shaders" action:nil keyEquivalent:@""];
    self.menu = [[NSMenu alloc] initWithTitle:@"Shaders"];
    self.menu.delegate = self;
    for (NSInteger shader = 0; shader < FE8_HOST_SHADER_COUNT; ++shader) {
        NSMenuItem *item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:
                fe8_host_shader_name((enum Fe8HostShader)shader)]
            action:@selector(selectShader:) keyEquivalent:@""];
        item.tag = shader;
        item.target = self;
        [self.menu addItem:item];
    }
    [self.menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *configure = [[NSMenuItem alloc]
        initWithTitle:@"Shader Settings…" action:@selector(showShaderSettings:)
        keyEquivalent:@""];
    configure.target = self;
    [self.menu addItem:configure];
    root.submenu = self.menu;
    [mainMenu addItem:root];
}

@end

static Fe8ShaderMenuController *shaderMenuController;

__attribute__((constructor))
static void fe8_register_shader_menu(void) {
    @autoreleasepool {
        [NSNotificationCenter.defaultCenter
            addObserverForName:NSApplicationDidFinishLaunchingNotification
            object:nil queue:NSOperationQueue.mainQueue
            usingBlock:^(NSNotification *note) {
                (void)note;
                if (!shaderMenuController) {
                    shaderMenuController = [[Fe8ShaderMenuController alloc] init];
                    [shaderMenuController installMenu];
                }
            }];
    }
}
