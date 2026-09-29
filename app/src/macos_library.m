#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "host_settings.h"
#include "macos_library.h"
#include "macos_settings.h"

static NSString *const kLibraryDefaultsKey = @"FE8GameLibrary";
static NSToolbarItemIdentifier const kToolbarAdd = @"fe8.add";
static NSToolbarItemIdentifier const kToolbarReveal = @"fe8.reveal";
static NSToolbarItemIdentifier const kToolbarRemove = @"fe8.remove";
static NSToolbarItemIdentifier const kToolbarResume = @"fe8.resume";
static NSToolbarItemIdentifier const kToolbarPlay = @"fe8.play";

static NSImage *symbol(NSString *name, NSString *description) {
    return [NSImage imageWithSystemSymbolName:name accessibilityDescription:description];
}

static NSArray<NSString *> *gbaPathsFromPasteboard(NSPasteboard *pasteboard) {
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    NSMutableArray<NSString *> *paths = [NSMutableArray array];
    for (NSURL *url in urls)
        if ([url.pathExtension caseInsensitiveCompare:@"gba"] == NSOrderedSame)
            [paths addObject:url.path];
    return paths;
}

@class Fe8LibraryController;

/* Content view: accepts dropped ROMs anywhere in the window and shows a
 * dashed accent outline while a compatible drag is hovering. */
@interface Fe8DropOverlayView : NSView
@end

@interface Fe8LibraryDropView : NSView <NSDraggingDestination>
@property(nonatomic, assign) Fe8LibraryController *controller;
@property(nonatomic, strong) Fe8DropOverlayView *overlay;
@end

@interface Fe8LibraryTableView : NSTableView
@property(nonatomic, assign) Fe8LibraryController *controller;
@end

/* One library row: generated cover badge, title, ROM details and progress. */
@interface Fe8GameCellView : NSTableCellView
@property(nonatomic, strong) NSView *badge;
@property(nonatomic, strong) NSTextField *initials;
@property(nonatomic, strong) NSTextField *titleField;
@property(nonatomic, strong) NSTextField *detailField;
@property(nonatomic, strong) NSTextField *statusField;
@property(nonatomic, strong) NSView *statusPill;
@property(nonatomic, strong) NSTextField *dateField;
@property(nonatomic, strong) NSColor *pillColor;
@end

@interface Fe8LibraryController : NSObject <NSApplicationDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSToolbarDelegate, NSMenuDelegate>
@property(nonatomic, copy) NSString *executablePath;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) Fe8LibraryTableView *table;
@property(nonatomic, strong) NSScrollView *scroll;
@property(nonatomic, strong) NSView *emptyView;
@property(nonatomic, strong) NSTextField *footerLabel;
@property(nonatomic, strong) NSBox *footerSeparator;
@property(nonatomic, strong) NSButton *playButton;
@property(nonatomic, strong) NSButton *resumeButton;
@property(nonatomic, strong) NSButton *removeButton;
@property(nonatomic, strong) NSButton *revealButton;
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *games;
@property(nonatomic, strong) NSMutableArray<NSTask *> *runningTasks;
@property(nonatomic, strong) NSRelativeDateTimeFormatter *relativeDates;
- (instancetype)initWithExecutablePath:(NSString *)path;
- (void)importPaths:(NSArray<NSString *> *)paths;
- (void)remove:(id)sender;
- (void)play:(id)sender;
@end

static NSString *applicationSupportRoot(void) {
    NSURL *base = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    return [[base URLByAppendingPathComponent:@"FE8 Extended Frontend"
        isDirectory:YES] path];
}

static NSString *sha1ForFile(NSString *path) {
    NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
    if (!data)
        return nil;
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *result = [NSMutableString stringWithCapacity:CC_SHA1_DIGEST_LENGTH * 2];
    for (NSUInteger i = 0; i < CC_SHA1_DIGEST_LENGTH; ++i)
        [result appendFormat:@"%02x", digest[i]];
    return result;
}

static NSString *cleanHeaderString(const unsigned char *bytes, NSUInteger length) {
    NSString *raw = [[NSString alloc] initWithBytes:bytes length:length
        encoding:NSASCIIStringEncoding];
    if (!raw)
        return @"";
    NSCharacterSet *trim = [NSCharacterSet characterSetWithCharactersInString:
        @" \t\r\n\0"];
    return [raw stringByTrimmingCharactersInSet:trim];
}

static NSMutableDictionary *gameForPath(NSString *path) {
    NSData *header = [NSData dataWithContentsOfFile:path
        options:NSDataReadingMappedIfSafe error:nil];
    if (header.length < 0xB0)
        return nil;
    NSString *identifier = sha1ForFile(path);
    if (!identifier)
        return nil;
    const unsigned char *bytes = header.bytes;
    NSString *internalTitle = cleanHeaderString(bytes + 0xA0, 12);
    NSString *gameCode = cleanHeaderString(bytes + 0xAC, 4);
    NSString *title = path.lastPathComponent.stringByDeletingPathExtension;
    return [@{
        @"id": identifier,
        @"path": path.stringByStandardizingPath,
        @"title": title.length ? title : @"Untitled GBA game",
        @"internalTitle": internalTitle,
        @"gameCode": gameCode,
    } mutableCopy];
}

/* Short cover monogram. Series words are skipped so hacks named
 * "Fire Emblem …" still get distinct badges. */
static NSString *monogramForTitle(NSString *title) {
    NSSet *skip = [NSSet setWithArray:@[@"fire", @"emblem", @"the", @"of", @"a", @"fe"]];
    NSMutableArray<NSString *> *words = [NSMutableArray array];
    for (NSString *word in [title componentsSeparatedByCharactersInSet:
            NSCharacterSet.whitespaceAndNewlineCharacterSet])
        if (word.length && ![skip containsObject:word.lowercaseString])
            [words addObject:word];
    if (!words.count && title.length)
        [words addObject:title];
    NSMutableString *result = [NSMutableString string];
    for (NSString *word in words) {
        if (result.length == 2) break;
        [result appendString:[word substringToIndex:1].uppercaseString];
    }
    return result.length ? result : @"?";
}

static NSColor *badgeColorForIdentifier(NSString *identifier) {
    static NSArray<NSColor *> *palette;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        palette = @[NSColor.systemBlueColor, NSColor.systemIndigoColor,
            NSColor.systemPurpleColor, NSColor.systemPinkColor, NSColor.systemRedColor,
            NSColor.systemOrangeColor, NSColor.systemTealColor, NSColor.systemGreenColor,
            NSColor.systemBrownColor];
    });
    unsigned hash = 0;
    for (NSUInteger i = 0; i < identifier.length && i < 8; ++i)
        hash = hash * 31u + [identifier characterAtIndex:i];
    return palette[hash % palette.count];
}

@implementation Fe8DropOverlayView

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor.windowBackgroundColor colorWithAlphaComponent:0.93] setFill];
    NSRectFill(self.bounds);
    NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:
        NSInsetRect(self.bounds, 14, 14) xRadius:14 yRadius:14];
    CGFloat dash[] = {8, 6};
    [outline setLineDash:dash count:2 phase:0];
    outline.lineWidth = 2.5;
    [[NSColor.controlAccentColor colorWithAlphaComponent:0.10] setFill];
    [outline fill];
    [NSColor.controlAccentColor setStroke];
    [outline stroke];
}

@end

@implementation Fe8LibraryDropView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (!self)
        return nil;
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    self.wantsLayer = YES;

    self.overlay = [[Fe8DropOverlayView alloc] initWithFrame:NSZeroRect];
    self.overlay.translatesAutoresizingMaskIntoConstraints = NO;
    self.overlay.hidden = YES;

    NSImageView *icon = [NSImageView imageViewWithImage:
        symbol(@"arrow.down.doc.fill", @"Drop")];
    icon.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:40 weight:NSFontWeightMedium];
    icon.contentTintColor = NSColor.controlAccentColor;
    NSTextField *label = [NSTextField labelWithString:@"Drop to add to your library"];
    label.font = [NSFont systemFontOfSize:17 weight:NSFontWeightSemibold];
    label.textColor = NSColor.controlAccentColor;
    NSStackView *stack = [NSStackView stackViewWithViews:@[icon, label]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.spacing = 10;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.overlay addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.centerXAnchor constraintEqualToAnchor:self.overlay.centerXAnchor],
        [stack.centerYAnchor constraintEqualToAnchor:self.overlay.centerYAnchor],
    ]];
    return self;
}

- (void)setDropHighlighted:(BOOL)highlighted {
    [self.overlay removeFromSuperview];
    if (highlighted) {
        [self addSubview:self.overlay positioned:NSWindowAbove relativeTo:nil];
        [NSLayoutConstraint activateConstraints:@[
            [self.overlay.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [self.overlay.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [self.overlay.topAnchor constraintEqualToAnchor:self.safeAreaLayoutGuide.topAnchor],
            [self.overlay.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
        self.overlay.needsDisplay = YES;
    }
    self.overlay.hidden = !highlighted;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    BOOL accepted = gbaPathsFromPasteboard(sender.draggingPasteboard).count > 0;
    [self setDropHighlighted:accepted];
    return accepted ? NSDragOperationCopy : NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    [self setDropHighlighted:NO];
}

- (void)draggingEnded:(id<NSDraggingInfo>)sender {
    (void)sender;
    [self setDropHighlighted:NO];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    [self setDropHighlighted:NO];
    NSArray<NSString *> *paths = gbaPathsFromPasteboard(sender.draggingPasteboard);
    [self.controller importPaths:paths];
    return paths.count > 0;
}

@end

@implementation Fe8LibraryTableView

- (void)keyDown:(NSEvent *)event {
    unsigned short code = event.keyCode;
    if ((code == 51 || code == 117) && self.selectedRow >= 0) {
        [self.controller remove:self];
        return;
    }
    if ((code == 36 || code == 76) && self.selectedRow >= 0) {
        [self.controller play:self];
        return;
    }
    [super keyDown:event];
}

@end

@implementation Fe8GameCellView

- (NSTextField *)label:(CGFloat)size weight:(NSFontWeight)weight color:(NSColor *)color {
    NSTextField *field = [NSTextField labelWithString:@""];
    field.font = [NSFont systemFontOfSize:size weight:weight];
    field.textColor = color;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
        forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (!self)
        return nil;
    self.badge = [[NSView alloc] init];
    self.badge.wantsLayer = YES;
    self.badge.layer.cornerRadius = 9;
    self.badge.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:self.badge];

    self.initials = [self label:15 weight:NSFontWeightBold color:NSColor.whiteColor];
    self.initials.alignment = NSTextAlignmentCenter;
    [self.badge addSubview:self.initials];

    self.titleField = [self label:14 weight:NSFontWeightSemibold color:NSColor.labelColor];
    self.detailField = [self label:11.5 weight:NSFontWeightRegular
        color:NSColor.secondaryLabelColor];
    self.detailField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self addSubview:self.titleField];
    [self addSubview:self.detailField];

    self.statusPill = [[NSView alloc] init];
    self.statusPill.wantsLayer = YES;
    self.statusPill.layer.cornerRadius = 9;
    self.statusPill.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:self.statusPill];
    self.statusField = [self label:11 weight:NSFontWeightSemibold color:NSColor.labelColor];
    [self.statusField setContentCompressionResistancePriority:NSLayoutPriorityRequired
        forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.statusPill addSubview:self.statusField];

    self.dateField = [self label:11 weight:NSFontWeightRegular
        color:NSColor.tertiaryLabelColor];
    self.dateField.alignment = NSTextAlignmentRight;
    [self.dateField setContentCompressionResistancePriority:NSLayoutPriorityDefaultHigh
        forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self addSubview:self.dateField];

    [NSLayoutConstraint activateConstraints:@[
        [self.badge.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
        [self.badge.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [self.badge.widthAnchor constraintEqualToConstant:38],
        [self.badge.heightAnchor constraintEqualToConstant:38],
        [self.initials.centerXAnchor constraintEqualToAnchor:self.badge.centerXAnchor],
        [self.initials.centerYAnchor constraintEqualToAnchor:self.badge.centerYAnchor],

        [self.titleField.leadingAnchor constraintEqualToAnchor:self.badge.trailingAnchor constant:12],
        [self.titleField.bottomAnchor constraintEqualToAnchor:self.centerYAnchor constant:1],
        [self.titleField.trailingAnchor constraintLessThanOrEqualToAnchor:self.statusPill.leadingAnchor constant:-12],
        [self.detailField.leadingAnchor constraintEqualToAnchor:self.titleField.leadingAnchor],
        [self.detailField.topAnchor constraintEqualToAnchor:self.centerYAnchor constant:3],
        [self.detailField.trailingAnchor constraintLessThanOrEqualToAnchor:self.dateField.leadingAnchor constant:-12],

        [self.statusPill.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-10],
        [self.statusPill.bottomAnchor constraintEqualToAnchor:self.centerYAnchor constant:1],
        [self.statusPill.heightAnchor constraintEqualToConstant:18],
        [self.statusField.leadingAnchor constraintEqualToAnchor:self.statusPill.leadingAnchor constant:8],
        [self.statusField.trailingAnchor constraintEqualToAnchor:self.statusPill.trailingAnchor constant:-8],
        [self.statusField.centerYAnchor constraintEqualToAnchor:self.statusPill.centerYAnchor],

        [self.dateField.trailingAnchor constraintEqualToAnchor:self.statusPill.trailingAnchor],
        [self.dateField.topAnchor constraintEqualToAnchor:self.centerYAnchor constant:3],
    ]];
    return self;
}

- (void)updatePillColors {
    [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
        BOOL emphasized = self.backgroundStyle == NSBackgroundStyleEmphasized;
        NSColor *color = self.pillColor ? self.pillColor : NSColor.systemGrayColor;
        self.statusPill.layer.backgroundColor = emphasized ?
            [NSColor.whiteColor colorWithAlphaComponent:0.22].CGColor :
            [color colorWithAlphaComponent:0.16].CGColor;
        self.statusField.textColor = emphasized ? NSColor.whiteColor : color;
        self.dateField.textColor = emphasized ?
            [NSColor.whiteColor colorWithAlphaComponent:0.75] : NSColor.tertiaryLabelColor;
    }];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle {
    [super setBackgroundStyle:backgroundStyle];
    [self updatePillColors];
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self updatePillColors];
}

@end

@implementation Fe8LibraryController

- (instancetype)initWithExecutablePath:(NSString *)path {
    self = [super init];
    if (!self)
        return nil;
    NSString *bundleExecutable = NSBundle.mainBundle.executablePath;
    self.executablePath = bundleExecutable ? bundleExecutable :
        path.stringByStandardizingPath;
    self.runningTasks = [NSMutableArray array];
    self.relativeDates = [[NSRelativeDateTimeFormatter alloc] init];
    self.relativeDates.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
    NSArray *stored = [NSUserDefaults.standardUserDefaults arrayForKey:kLibraryDefaultsKey];
    self.games = [NSMutableArray array];
    for (NSDictionary *game in stored)
        if ([game[@"id"] isKindOfClass:NSString.class] &&
                [game[@"path"] isKindOfClass:NSString.class])
            [self.games addObject:[game mutableCopy]];
    [self buildWindow];
    return self;
}

- (NSView *)buildEmptyView {
    NSImageView *icon = [NSImageView imageViewWithImage:
        symbol(@"gamecontroller", @"No games")];
    icon.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:52 weight:NSFontWeightLight];
    icon.contentTintColor = NSColor.tertiaryLabelColor;

    NSTextField *heading = [NSTextField labelWithString:@"No Games Yet"];
    heading.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];

    NSTextField *body = [NSTextField wrappingLabelWithString:
        @"Drag Game Boy Advance ROMs (.gba) into this window, or choose them from disk. "
        @"Each game keeps its own saves and quick state."];
    body.alignment = NSTextAlignmentCenter;
    body.textColor = NSColor.secondaryLabelColor;
    body.preferredMaxLayoutWidth = 360;

    NSButton *add = [NSButton buttonWithTitle:@"Add ROMs…" target:self action:@selector(addRoms:)];
    add.controlSize = NSControlSizeLarge;
    add.bezelColor = NSColor.controlAccentColor;

    NSStackView *stack = [NSStackView stackViewWithViews:@[icon, heading, body, add]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.spacing = 10;
    [stack setCustomSpacing:18 afterView:icon];
    [stack setCustomSpacing:20 afterView:body];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    return stack;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(0, 0, 820, 560);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
            NSWindowStyleMaskFullSizeContentView
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"FE8 Library";
    self.window.minSize = NSMakeSize(560, 380);
    self.window.releasedWhenClosed = NO;
    self.window.toolbarStyle = NSWindowToolbarStyleUnified;
    self.window.frameAutosaveName = @"FE8LibraryWindow";

    NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"FE8LibraryToolbar"];
    toolbar.delegate = self;
    toolbar.displayMode = NSToolbarDisplayModeIconOnly;
    toolbar.allowsUserCustomization = NO;
    self.window.toolbar = toolbar;

    Fe8LibraryDropView *content = [[Fe8LibraryDropView alloc] initWithFrame:frame];
    content.controller = self;
    self.window.contentView = content;

    self.scroll = [[NSScrollView alloc] init];
    self.scroll.translatesAutoresizingMaskIntoConstraints = NO;
    self.scroll.hasVerticalScroller = YES;
    self.scroll.autohidesScrollers = YES;
    self.scroll.borderType = NSNoBorder;
    self.scroll.drawsBackground = NO;

    self.table = [[Fe8LibraryTableView alloc] initWithFrame:NSZeroRect];
    self.table.controller = self;
    self.table.style = NSTableViewStyleInset;
    self.table.headerView = nil;
    self.table.backgroundColor = NSColor.clearColor;
    self.table.rowHeight = 58;
    self.table.intercellSpacing = NSMakeSize(0, 4);
    self.table.allowsEmptySelection = YES;
    self.table.allowsMultipleSelection = NO;
    self.table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    self.table.dataSource = self;
    self.table.delegate = self;
    self.table.target = self;
    self.table.doubleAction = @selector(tableDoubleClicked:);
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"game"];
    column.resizingMask = NSTableColumnAutoresizingMask;
    [self.table addTableColumn:column];
    self.table.menu = [[NSMenu alloc] initWithTitle:@"Game"];
    self.table.menu.delegate = self;
    self.scroll.documentView = self.table;
    [content addSubview:self.scroll];

    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    separator.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:separator];
    self.footerSeparator = separator;

    self.footerLabel = [NSTextField labelWithString:@""];
    self.footerLabel.font = [NSFont systemFontOfSize:11];
    self.footerLabel.textColor = NSColor.secondaryLabelColor;
    self.footerLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    self.footerLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:self.footerLabel];

    self.emptyView = [self buildEmptyView];
    [content addSubview:self.emptyView];

    [NSLayoutConstraint activateConstraints:@[
        [self.scroll.topAnchor constraintEqualToAnchor:content.topAnchor],
        [self.scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [self.scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [self.scroll.bottomAnchor constraintEqualToAnchor:separator.topAnchor],
        [separator.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [separator.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [separator.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-28],
        [self.footerLabel.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [self.footerLabel.trailingAnchor constraintLessThanOrEqualToAnchor:content.trailingAnchor constant:-16],
        [self.footerLabel.centerYAnchor constraintEqualToAnchor:content.bottomAnchor constant:-14],
        [self.emptyView.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],
        [self.emptyView.centerYAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.centerYAnchor constant:-14],
    ]];

    if (self.games.count)
        [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    [self updateControls];
    if (![self.window setFrameUsingName:self.window.frameAutosaveName])
        [self.window center];
}

#pragma mark Toolbar

- (NSButton *)toolbarButton:(NSString *)symbolName title:(NSString *)title
    tooltip:(NSString *)tooltip action:(SEL)action {
    NSButton *button = [NSButton buttonWithImage:symbol(symbolName, title)
        target:self action:action];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.toolTip = tooltip;
    if (title) {
        button.title = title;
        button.imagePosition = NSImageLeading;
        button.imageHugsTitle = YES;
    }
    return button;
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    (void)toolbar;
    return @[kToolbarAdd, NSToolbarFlexibleSpaceItemIdentifier, kToolbarReveal,
        kToolbarRemove, NSToolbarSpaceItemIdentifier, kToolbarResume, kToolbarPlay];
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
    return [self toolbarDefaultItemIdentifiers:toolbar];
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar;
    (void)flag;
    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
    item.autovalidates = NO;
    NSButton *button = nil;
    if ([identifier isEqualToString:kToolbarAdd]) {
        button = [self toolbarButton:@"plus" title:nil
            tooltip:@"Add ROMs to the library (⌘O)" action:@selector(addRoms:)];
        item.label = @"Add ROMs";
    } else if ([identifier isEqualToString:kToolbarReveal]) {
        button = [self toolbarButton:@"folder" title:nil
            tooltip:@"Show this game's saves in Finder" action:@selector(revealSaves:)];
        item.label = @"Show Saves";
        self.revealButton = button;
    } else if ([identifier isEqualToString:kToolbarRemove]) {
        button = [self toolbarButton:@"trash" title:nil
            tooltip:@"Remove from the library (saves and ROM are kept)" action:@selector(remove:)];
        item.label = @"Remove";
        self.removeButton = button;
    } else if ([identifier isEqualToString:kToolbarResume]) {
        button = [self toolbarButton:@"clock.arrow.circlepath" title:@"Resume"
            tooltip:@"Resume from this game's latest quick state (⌘R)" action:@selector(resume:)];
        item.label = @"Resume State";
        self.resumeButton = button;
    } else if ([identifier isEqualToString:kToolbarPlay]) {
        button = [self toolbarButton:@"play.fill" title:@"Play"
            tooltip:@"Start from the cartridge save (Return)" action:@selector(play:)];
        button.bezelStyle = NSBezelStylePush;
        button.keyEquivalent = @"\r";
        item.label = @"Play";
        self.playButton = button;
    } else {
        return nil;
    }
    item.view = button;
    item.toolTip = button.toolTip;
    [self updateControls];
    return item;
}

#pragma mark Menus

- (void)installMenu {
    NSString *appName = @"FE8 Library";
    NSMenu *main = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appRoot = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:appName];
    [appMenu addItemWithTitle:[@"About " stringByAppendingString:appName]
        action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:appName]
        action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem *hideOthers = [appMenu addItemWithTitle:@"Hide Others"
        action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:NSMenuItem.separatorItem];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName]
        action:@selector(terminate:) keyEquivalent:@"q"];
    appRoot.submenu = appMenu;
    [main addItem:appRoot];

    NSMenuItem *fileRoot = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *add = [fileMenu addItemWithTitle:@"Add ROMs…"
        action:@selector(addRoms:) keyEquivalent:@"o"];
    add.target = self;
    [fileMenu addItem:NSMenuItem.separatorItem];
    [fileMenu addItemWithTitle:@"Play" action:@selector(play:) keyEquivalent:@""].target = self;
    [fileMenu addItemWithTitle:@"Resume State" action:@selector(resume:)
        keyEquivalent:@"r"].target = self;
    [fileMenu addItem:NSMenuItem.separatorItem];
    [fileMenu addItemWithTitle:@"Show Saves in Finder" action:@selector(revealSaves:)
        keyEquivalent:@""].target = self;
    NSMenuItem *remove = [fileMenu addItemWithTitle:@"Remove from Library"
        action:@selector(remove:) keyEquivalent:@"\b"];
    remove.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    remove.target = self;
    [fileMenu addItem:NSMenuItem.separatorItem];
    [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    fileRoot.submenu = fileMenu;
    [main addItem:fileRoot];

    NSMenuItem *windowRoot = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    windowRoot.submenu = windowMenu;
    [main addItem:windowRoot];
    NSApp.mainMenu = main;
    NSApp.windowsMenu = windowMenu;
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    [menu removeAllItems];
    NSInteger row = self.table.clickedRow;
    if (row < 0 || row >= (NSInteger)self.games.count)
        return;
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    [menu addItemWithTitle:@"Play" action:@selector(play:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:@"Resume State" action:@selector(resume:) keyEquivalent:@""].target = self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"Show Saves in Finder" action:@selector(revealSaves:)
        keyEquivalent:@""].target = self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"Remove from Library" action:@selector(remove:)
        keyEquivalent:@""].target = self;
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    SEL action = item.action;
    if (action == @selector(play:)) return self.playButton ? self.playButton.enabled : [self canPlay];
    if (action == @selector(resume:)) return [self canResume];
    if (action == @selector(remove:) || action == @selector(revealSaves:))
        return [self selectedGame] != nil;
    return YES;
}

- (void)show {
    [self installMenu];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.table];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [self reloadPreservingSelection];
}

- (void)reloadPreservingSelection {
    NSIndexSet *selection = self.table.selectedRowIndexes;
    [self.table reloadData];
    [self.table selectRowIndexes:selection byExtendingSelection:NO];
    [self updateControls];
}

#pragma mark Table

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return self.games.count;
}

- (NSDate *)modificationDateAtPath:(NSString *)path {
    return [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column
    row:(NSInteger)row {
    (void)column;
    Fe8GameCellView *cell = [tableView makeViewWithIdentifier:@"game" owner:self];
    if (!cell) {
        cell = [[Fe8GameCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = @"game";
    }
    NSDictionary *game = self.games[row];
    NSString *title = game[@"title"] ? game[@"title"] : @"Untitled";
    cell.titleField.stringValue = title;
    cell.initials.stringValue = monogramForTitle(title);
    cell.badge.layer.backgroundColor = badgeColorForIdentifier(game[@"id"]).CGColor;

    NSString *internal = game[@"internalTitle"] ? game[@"internalTitle"] : @"";
    NSString *code = game[@"gameCode"] ? game[@"gameCode"] : @"";
    NSMutableArray<NSString *> *details = [NSMutableArray array];
    if (internal.length) [details addObject:internal];
    if (code.length) [details addObject:code];
    NSString *path = game[@"path"] ? game[@"path"] : @"";
    [details addObject:path.stringByAbbreviatingWithTildeInPath];
    cell.detailField.stringValue = [details componentsJoinedByString:@"  ·  "];
    cell.toolTip = path;

    NSFileManager *files = NSFileManager.defaultManager;
    NSString *statePath = [self statePathForGame:game];
    NSString *savePath = [self savePathForGame:game];
    NSDate *date = nil;
    if (![files fileExistsAtPath:path]) {
        cell.statusField.stringValue = @"ROM Missing";
        cell.pillColor = NSColor.systemRedColor;
        cell.badge.layer.backgroundColor = NSColor.systemGrayColor.CGColor;
    } else if ([files fileExistsAtPath:statePath]) {
        cell.statusField.stringValue = @"Quick State";
        cell.pillColor = NSColor.systemGreenColor;
        date = [self modificationDateAtPath:statePath];
    } else if ([files fileExistsAtPath:savePath]) {
        cell.statusField.stringValue = @"Cartridge Save";
        cell.pillColor = NSColor.systemBlueColor;
        date = [self modificationDateAtPath:savePath];
    } else {
        cell.statusField.stringValue = @"Not Played";
        cell.pillColor = NSColor.systemGrayColor;
    }
    cell.dateField.stringValue = date ? [NSString stringWithFormat:@"Saved %@",
        [self.relativeDates localizedStringForDate:date relativeToDate:NSDate.date]] : @"";
    [cell updatePillColors];
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    (void)notification;
    [self updateControls];
}

- (void)tableDoubleClicked:(id)sender {
    (void)sender;
    if (self.table.clickedRow >= 0)
        [self play:sender];
}

#pragma mark Model

- (NSDictionary *)selectedGame {
    NSInteger row = self.table.selectedRow;
    return row >= 0 && row < (NSInteger)self.games.count ? self.games[row] : nil;
}

- (NSString *)directoryForGame:(NSDictionary *)game {
    return [[applicationSupportRoot() stringByAppendingPathComponent:@"Games"]
        stringByAppendingPathComponent:game[@"id"]];
}

- (NSString *)savePathForGame:(NSDictionary *)game {
    return [[self directoryForGame:game] stringByAppendingPathComponent:@"cartridge.sav"];
}

- (NSString *)statePathForGame:(NSDictionary *)game {
    return [[self directoryForGame:game] stringByAppendingPathComponent:@"quick-state.ss"];
}

- (void)migrateAdjacentSaveForGame:(NSDictionary *)game {
    NSString *destination = [self savePathForGame:game];
    if ([NSFileManager.defaultManager fileExistsAtPath:destination])
        return;
    NSString *rom = game[@"path"];
    NSString *adjacent = [[rom stringByDeletingPathExtension]
        stringByAppendingPathExtension:@"sav"];
    if ([NSFileManager.defaultManager fileExistsAtPath:adjacent])
        [NSFileManager.defaultManager copyItemAtPath:adjacent
            toPath:destination error:nil];
}

- (BOOL)canPlay {
    NSDictionary *game = [self selectedGame];
    return game && [NSFileManager.defaultManager fileExistsAtPath:game[@"path"]];
}

- (BOOL)canResume {
    NSDictionary *game = [self selectedGame];
    return [self canPlay] &&
        [NSFileManager.defaultManager fileExistsAtPath:[self statePathForGame:game]];
}

- (void)updateControls {
    BOOL selected = [self selectedGame] != nil;
    self.playButton.enabled = [self canPlay];
    self.resumeButton.enabled = [self canResume];
    self.removeButton.enabled = selected;
    self.revealButton.enabled = selected;
    BOOL empty = self.games.count == 0;
    self.emptyView.hidden = !empty;
    self.scroll.hidden = empty;
    NSUInteger count = self.games.count;
    self.window.subtitle = empty ? @"" : (count == 1 ? @"1 game" :
        [NSString stringWithFormat:@"%lu games", (unsigned long)count]);
    self.footerLabel.stringValue =
        @"Double-click a game to play  ·  Drop .gba files anywhere to add them";
    self.footerLabel.hidden = empty;
    self.footerSeparator.hidden = empty;
}

- (void)saveLibrary {
    [NSUserDefaults.standardUserDefaults setObject:self.games forKey:kLibraryDefaultsKey];
}

#pragma mark Actions

- (void)addRoms:(id)sender {
    (void)sender;
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.title = @"Add Game Boy Advance ROMs";
    panel.message = @"Choose one or more .gba files to add to your library.";
    panel.prompt = @"Add to Library";
    panel.allowsMultipleSelection = YES;
    panel.canChooseDirectories = NO;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"gba"]];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result != NSModalResponseOK)
            return;
        NSMutableArray<NSString *> *paths = [NSMutableArray array];
        for (NSURL *url in panel.URLs)
            [paths addObject:url.path];
        [self importPaths:paths];
    }];
}

- (void)importPaths:(NSArray<NSString *> *)paths {
    NSString *lastImportedIdentifier = nil;
    for (NSString *path in paths) {
        if ([path.pathExtension caseInsensitiveCompare:@"gba"] != NSOrderedSame)
            continue;
        NSMutableDictionary *game = gameForPath(path);
        if (!game)
            continue;
        NSUInteger duplicate = [self.games indexOfObjectPassingTest:
            ^BOOL(NSDictionary *candidate, NSUInteger index, BOOL *stop) {
                (void)index;
                (void)stop;
                return [candidate[@"id"] isEqualToString:game[@"id"]];
            }];
        if (duplicate == NSNotFound) {
            [self.games addObject:game];
        } else {
            self.games[duplicate] = game;
        }
        lastImportedIdentifier = game[@"id"];
    }
    [self.games sortUsingComparator:^NSComparisonResult(NSDictionary *left, NSDictionary *right) {
        return [left[@"title"] localizedCaseInsensitiveCompare:right[@"title"]];
    }];
    [self saveLibrary];
    [self.table reloadData];
    if (lastImportedIdentifier) {
        NSUInteger selected = [self.games indexOfObjectPassingTest:
            ^BOOL(NSDictionary *candidate, NSUInteger index, BOOL *stop) {
                (void)index;
                (void)stop;
                return [candidate[@"id"] isEqualToString:lastImportedIdentifier];
            }];
        if (selected != NSNotFound) {
            [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:selected]
                byExtendingSelection:NO];
            [self.table scrollRowToVisible:selected];
        }
    }
    [self updateControls];
}

- (void)remove:(id)sender {
    (void)sender;
    NSInteger row = self.table.selectedRow;
    if (row < 0 || row >= (NSInteger)self.games.count)
        return;
    [self.games removeObjectAtIndex:row];
    [self saveLibrary];
    [self.table reloadData];
    if (self.games.count) {
        NSUInteger next = (NSUInteger)row < self.games.count ? (NSUInteger)row :
            self.games.count - 1;
        [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:next]
            byExtendingSelection:NO];
    }
    [self updateControls];
}

- (void)revealSaves:(id)sender {
    (void)sender;
    NSDictionary *game = [self selectedGame];
    if (!game)
        return;
    NSString *directory = [self directoryForGame:game];
    [NSFileManager.defaultManager createDirectoryAtPath:directory
        withIntermediateDirectories:YES attributes:nil error:nil];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
        @[[NSURL fileURLWithPath:directory isDirectory:YES]]];
}

- (void)launchGame:(NSDictionary *)game resume:(BOOL)resume {
    if (!game)
        return;
    NSString *directory = [self directoryForGame:game];
    NSError *directoryError = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:directory
            withIntermediateDirectories:YES attributes:nil error:&directoryError]) {
        [self showError:directoryError.localizedDescription];
        return;
    }
    [self migrateAdjacentSaveForGame:game];
    NSString *state = [self statePathForGame:game];
    NSMutableArray<NSString *> *arguments = [@[
        @"--rom", game[@"path"],
        @"--save", [self savePathForGame:game],
        @"--quick-state", state,
    ] mutableCopy];
    if (resume && [NSFileManager.defaultManager fileExistsAtPath:state])
        [arguments addObjectsFromArray:@[@"--state", state]];

    NSTask *task = [[NSTask alloc] init];
    task.launchPath = self.executablePath;
    task.arguments = arguments;
    task.standardInput = NSFileHandle.fileHandleWithNullDevice;
    task.standardOutput = NSFileHandle.fileHandleWithNullDevice;
    task.standardError = NSFileHandle.fileHandleWithNullDevice;
    [self.runningTasks addObject:task];
    task.terminationHandler = ^(NSTask *finished) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.runningTasks removeObject:finished];
            [self reloadPreservingSelection];
        });
    };
    @try {
        [task launch];
        pid_t processIdentifier = task.processIdentifier;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
            dispatch_get_main_queue(), ^{
                NSRunningApplication *running = [NSRunningApplication
                    runningApplicationWithProcessIdentifier:processIdentifier];
                [running activateWithOptions:0];
            });
    } @catch (NSException *exception) {
        [self.runningTasks removeObject:task];
        [self showError:exception.reason];
    }
}

- (void)play:(id)sender {
    (void)sender;
    if ([self canPlay])
        [self launchGame:[self selectedGame] resume:NO];
}

- (void)resume:(id)sender {
    (void)sender;
    if ([self canResume])
        [self launchGame:[self selectedGame] resume:YES];
}

- (void)showError:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"Unable to launch game";
    alert.informativeText = message ? message : @"An unknown error occurred.";
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

@end

static Fe8LibraryController *libraryController;
static Fe8HostSettings librarySettings;

int fe8_macos_run_library(const char *executable_path) {
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSWindow.allowsAutomaticWindowTabbing = NO;
        fe8_host_settings_init(&librarySettings);
        fe8_macos_load_settings(&librarySettings);
        libraryController = [[Fe8LibraryController alloc]
            initWithExecutablePath:[NSString stringWithUTF8String:executable_path]];
        application.delegate = libraryController;
        [application finishLaunching];
        [libraryController show];
        fe8_macos_install_settings_menu(&librarySettings,
            NULL, NULL, NULL, NULL);
        [application run];
    }
    return 0;
}
