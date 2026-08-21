#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "gba_rom_info.h"
#include "host_settings.h"
#include "macos_library.h"
#include "macos_settings.h"

static NSString *const kLibraryDefaultsKey = @"FE8GameLibrary";

@class Fe8LibraryController;

@interface Fe8LibraryDropView : NSView <NSDraggingDestination>
@property(nonatomic, assign) Fe8LibraryController *controller;
@end

@interface Fe8LibraryController : NSObject <NSApplicationDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSMenuItemValidation>
@property(nonatomic, copy) NSString *executablePath;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTableView *table;
@property(nonatomic, strong) NSTextField *emptyLabel;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSButton *playButton;
@property(nonatomic, strong) NSButton *resumeButton;
@property(nonatomic, strong) NSButton *removeButton;
@property(nonatomic, strong) NSButton *revealRomButton;
@property(nonatomic, strong) NSButton *revealButton;
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *games;
@property(nonatomic, strong) NSMutableArray<NSTask *> *runningTasks;
@property(nonatomic, strong) NSMutableSet<NSString *> *runningGameIdentifiers;
@property(nonatomic, strong) NSDateFormatter *dateFormatter;
- (instancetype)initWithExecutablePath:(NSString *)path;
- (void)importPaths:(NSArray<NSString *> *)paths;
@end

@interface Fe8LibraryController ()
- (void)buildWindow;
- (void)installMenu;
- (void)show;
- (void)updateControls;
- (NSMutableDictionary *)selectedGame;
- (NSMutableDictionary *)gameWithIdentifier:(NSString *)identifier;
- (NSString *)directoryForGame:(NSDictionary *)game;
- (NSString *)savePathForGame:(NSDictionary *)game;
- (NSString *)statePathForGame:(NSDictionary *)game;
- (NSString *)runLogPathForGame:(NSDictionary *)game;
- (NSString *)statusForGame:(NSDictionary *)game;
- (BOOL)romIsReadableForGame:(NSDictionary *)game;
- (BOOL)migrateAdjacentSaveForGame:(NSDictionary *)game error:(NSError **)error;
- (void)saveLibrary;
- (void)addRoms:(id)sender;
- (void)remove:(id)sender;
- (void)revealOrLocateRom:(id)sender;
- (void)revealSaves:(id)sender;
- (void)play:(id)sender;
- (void)resume:(id)sender;
- (void)showAbout:(id)sender;
- (void)showErrorWithTitle:(NSString *)title message:(NSString *)message;
@end

static NSString *applicationSupportRoot(void) {
    NSURL *base = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    return [[base URLByAppendingPathComponent:@"FE8 Extended Frontend"
        isDirectory:YES] path];
}

static NSString *stringFromUtf8(const char *text, NSString *fallback) {
    NSString *result = text ? [NSString stringWithUTF8String:text] : nil;
    return result ? result : fallback;
}

static NSString *bundledTextAtRelativePath(NSString *relativePath) {
    NSURL *resourceRoot = NSBundle.mainBundle.resourceURL;
    NSURL *url = [resourceRoot URLByAppendingPathComponent:relativePath];
    NSError *error = nil;
    NSString *text = [NSString stringWithContentsOfURL:url
        encoding:NSUTF8StringEncoding error:&error];
    if (text.length)
        return text;
    return [NSString stringWithFormat:@"Unable to load %@%@", relativePath,
        error.localizedDescription.length ?
            [@": " stringByAppendingString:error.localizedDescription] : @"."];
}

static NSAttributedString *aboutCredits(void) {
    NSArray<NSArray<NSString *> *> *documents = @[
        @[@"FE8 Extended Frontend — MIT License", @"LICENSE"],
        @[@"Third-Party Notices", @"THIRD_PARTY_NOTICES.md"],
        @[@"mGBA License", @"ThirdParty/mGBA/LICENSE"],
    ];
    NSMutableString *credits = [NSMutableString stringWithString:
        @"This frontend does not include ROMs or game assets.\n\n"];
    for (NSArray<NSString *> *document in documents) {
        [credits appendFormat:@"%@\n%@\n\n", document[0],
            bundledTextAtRelativePath(document[1])];
    }
    return [[NSAttributedString alloc] initWithString:credits attributes:@{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:10
            weight:NSFontWeightRegular],
    }];
}

static NSString *abbreviatedList(NSArray<NSString *> *items, NSUInteger limit) {
    if (!items.count)
        return @"";
    NSUInteger displayed = MIN(items.count, limit);
    NSArray<NSString *> *visible = [items subarrayWithRange:NSMakeRange(0, displayed)];
    NSString *result = [visible componentsJoinedByString:@"\n"];
    if (items.count > displayed)
        result = [result stringByAppendingFormat:@"\n…and %lu more",
            (unsigned long)(items.count - displayed)];
    return result;
}

static NSString *tailOfTextFile(NSString *path, NSUInteger maximumBytes) {
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:path];
    if (!handle)
        return @"";
    unsigned long long size = [handle seekToEndOfFile];
    unsigned long long start = size > maximumBytes ? size - maximumBytes : 0;
    [handle seekToFileOffset:start];
    NSData *data = [handle readDataToEndOfFile];
    [handle closeFile];
    NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!text)
        text = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];
    return [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

static NSMutableDictionary *gameForPath(
    NSString *path, NSString **errorMessage, NSString **warningMessage) {
    Fe8GbaRomInfo info;
    char error[1024];
    char warnings[1024];
    Fe8GbaRomReadResult result = fe8_gba_rom_info_read_file(
        path.fileSystemRepresentation, &info, error, sizeof(error));
    if (result != FE8_GBA_ROM_READ_OK || !fe8_gba_rom_info_is_importable(&info)) {
        if (errorMessage) {
            NSString *reason = stringFromUtf8(error, @"The file is not a valid GBA ROM.");
            *errorMessage = reason.length ? reason : @"The file is not a valid GBA ROM.";
        }
        return nil;
    }

    fe8_gba_rom_info_format_issues(&info, warnings, sizeof(warnings));
    if (warningMessage)
        *warningMessage = warnings[0] ? stringFromUtf8(warnings, @"") : nil;

    NSString *title = path.lastPathComponent.stringByDeletingPathExtension;
    NSString *internalTitle = stringFromUtf8(info.internal_title, @"");
    NSString *gameCode = stringFromUtf8(info.game_code, @"");
    NSString *makerCode = stringFromUtf8(info.maker_code, @"");
    NSString *identifier = stringFromUtf8(info.sha1, @"");
    NSString *compatibility = stringFromUtf8(
        fe8_gba_rom_info_compatibility_label(&info), @"Unknown");
    NSMutableDictionary *game = [@{
        @"id": identifier,
        @"path": path.stringByStandardizingPath,
        @"title": title.length ? title : @"Untitled GBA game",
        @"internalTitle": internalTitle,
        @"gameCode": gameCode,
        @"makerCode": makerCode,
        @"revision": @(info.version),
        @"fileSize": @(info.file_size),
        @"compatibility": compatibility,
        @"headerChecksumValid": @(!(info.issues & FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM)),
    } mutableCopy];
    if (warnings[0])
        game[@"validationWarnings"] = stringFromUtf8(warnings, @"");
    return game;
}

@implementation Fe8LibraryDropView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        self.wantsLayer = YES;
    }
    return self;
}

- (NSArray<NSString *> *)romPathsFromDraggingInfo:(id<NSDraggingInfo>)sender {
    NSArray<NSURL *> *urls = [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    NSMutableArray<NSString *> *paths = [NSMutableArray array];
    for (NSURL *url in urls) {
        if ([url.pathExtension caseInsensitiveCompare:@"gba"] == NSOrderedSame)
            [paths addObject:url.path];
    }
    return paths;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    if (![self romPathsFromDraggingInfo:sender].count)
        return NSDragOperationNone;
    self.layer.backgroundColor =
        [NSColor.controlAccentColor colorWithAlphaComponent:0.10].CGColor;
    return NSDragOperationCopy;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return [self romPathsFromDraggingInfo:sender].count ?
        NSDragOperationCopy : NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
    NSArray<NSString *> *paths = [self romPathsFromDraggingInfo:sender];
    [self.controller importPaths:paths];
    return paths.count > 0;
}

- (void)concludeDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
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
    self.runningGameIdentifiers = [NSMutableSet set];
    self.dateFormatter = [[NSDateFormatter alloc] init];
    self.dateFormatter.dateStyle = NSDateFormatterMediumStyle;
    self.dateFormatter.timeStyle = NSDateFormatterShortStyle;
    self.dateFormatter.doesRelativeDateFormatting = YES;

    NSArray *stored = [NSUserDefaults.standardUserDefaults arrayForKey:kLibraryDefaultsKey];
    self.games = [NSMutableArray array];
    for (NSDictionary *storedGame in stored) {
        if (![storedGame[@"id"] isKindOfClass:NSString.class] ||
                ![storedGame[@"path"] isKindOfClass:NSString.class])
            continue;
        NSMutableDictionary *game = [storedGame mutableCopy];
        if (![game[@"compatibility"] isKindOfClass:NSString.class]) {
            NSString *code = [game[@"gameCode"] isKindOfClass:NSString.class] ?
                game[@"gameCode"] : @"";
            if ([code isEqualToString:@"BE8E"])
                game[@"compatibility"] = @"FE8U-compatible";
            else if ([code hasPrefix:@"BE8"])
                game[@"compatibility"] = @"FE8-family; extensions may vary";
            else
                game[@"compatibility"] = @"GBA; standard rendering";
        }
        [self.games addObject:game];
    }
    [self buildWindow];
    return self;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(0, 0, 1080, 620);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"FE8 Extended Frontend";
    self.window.minSize = NSMakeSize(820, 480);
    self.window.releasedWhenClosed = NO;

    Fe8LibraryDropView *content = [[Fe8LibraryDropView alloc] initWithFrame:frame];
    content.controller = self;
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.window.contentView = content;

    NSTextField *title = [NSTextField labelWithString:@"Game Library"];
    title.frame = NSMakeRect(28, 558, 600, 38);
    title.font = [NSFont systemFontOfSize:28 weight:NSFontWeightSemibold];
    title.autoresizingMask = NSViewMinYMargin;
    [content addSubview:title];

    NSTextField *subtitle = [NSTextField labelWithString:
        @"ROMs stay in place. Saves, quick states, and diagnostics are isolated by ROM hash."];
    subtitle.frame = NSMakeRect(30, 530, 760, 22);
    subtitle.textColor = NSColor.secondaryLabelColor;
    subtitle.autoresizingMask = NSViewMinYMargin;
    [content addSubview:subtitle];

    NSButton *add = [NSButton buttonWithTitle:@"Add ROMs…" target:self action:@selector(addRoms:)];
    add.frame = NSMakeRect(916, 551, 140, 34);
    add.bezelStyle = NSBezelStyleRounded;
    add.keyEquivalent = @"o";
    add.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    add.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    add.toolTip = @"Import one or more legally obtained .gba files";
    [content addSubview:add];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(24, 92, 1032, 422)];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSBezelBorder;
    scroll.drawsBackground = YES;

    self.table = [[NSTableView alloc] initWithFrame:scroll.bounds];
    self.table.dataSource = self;
    self.table.delegate = self;
    self.table.rowHeight = 36;
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.allowsEmptySelection = YES;
    self.table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    self.table.doubleAction = @selector(play:);
    self.table.target = self;
    NSArray *columns = @[
        @[@"title", @"Title", @220],
        @[@"code", @"Game", @145],
        @[@"compatibility", @"Compatibility", @170],
        @[@"path", @"Location", @220],
        @[@"last", @"Last Played", @140],
        @[@"save", @"Progress", @115],
    ];
    for (NSArray *definition in columns) {
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:definition[0]];
        column.title = definition[1];
        column.width = [definition[2] doubleValue];
        column.minWidth = [definition[0] isEqualToString:@"path"] ? 140 : 80;
        [self.table addTableColumn:column];
    }
    scroll.documentView = self.table;
    [content addSubview:scroll];

    self.emptyLabel = [NSTextField labelWithString:
        @"No games imported yet\n\nDrop one or more .gba files anywhere in this window, or choose Add ROMs…"];
    self.emptyLabel.frame = NSMakeRect(190, 268, 700, 78);
    self.emptyLabel.alignment = NSTextAlignmentCenter;
    self.emptyLabel.font = [NSFont systemFontOfSize:16 weight:NSFontWeightMedium];
    self.emptyLabel.textColor = NSColor.secondaryLabelColor;
    self.emptyLabel.maximumNumberOfLines = 3;
    self.emptyLabel.autoresizingMask = NSViewMinXMargin | NSViewMaxXMargin |
        NSViewMinYMargin | NSViewMaxYMargin;
    [content addSubview:self.emptyLabel];

    self.removeButton = [NSButton buttonWithTitle:@"Remove" target:self action:@selector(remove:)];
    self.removeButton.frame = NSMakeRect(24, 32, 92, 32);
    self.removeButton.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    self.removeButton.toolTip = @"Remove the library reference; ROMs and saves stay on disk";
    [content addSubview:self.removeButton];

    self.revealRomButton = [NSButton buttonWithTitle:@"Show ROM" target:self
        action:@selector(revealOrLocateRom:)];
    self.revealRomButton.frame = NSMakeRect(126, 32, 112, 32);
    self.revealRomButton.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    self.revealRomButton.toolTip = @"Reveal the selected ROM, or locate it if it moved";
    [content addSubview:self.revealRomButton];

    self.revealButton = [NSButton buttonWithTitle:@"Show Saves" target:self
        action:@selector(revealSaves:)];
    self.revealButton.frame = NSMakeRect(248, 32, 112, 32);
    self.revealButton.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    self.revealButton.toolTip = @"Reveal this game's isolated save, state, and run log folder";
    [content addSubview:self.revealButton];

    self.statusLabel = [NSTextField labelWithString:@""];
    self.statusLabel.frame = NSMakeRect(378, 37, 390, 20);
    self.statusLabel.alignment = NSTextAlignmentCenter;
    self.statusLabel.textColor = NSColor.secondaryLabelColor;
    self.statusLabel.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [content addSubview:self.statusLabel];

    self.resumeButton = [NSButton buttonWithTitle:@"Resume State" target:self action:@selector(resume:)];
    self.resumeButton.frame = NSMakeRect(806, 32, 120, 32);
    self.resumeButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    self.resumeButton.toolTip = @"Load this game's latest isolated quick state";
    [content addSubview:self.resumeButton];

    self.playButton = [NSButton buttonWithTitle:@"Play" target:self action:@selector(play:)];
    self.playButton.frame = NSMakeRect(936, 32, 120, 32);
    self.playButton.bezelStyle = NSBezelStyleRounded;
    self.playButton.keyEquivalent = @"\r";
    self.playButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    self.playButton.toolTip = @"Start from the cartridge save";
    [content addSubview:self.playButton];

    [self updateControls];
    [self.window center];
}

- (void)installMenu {
    NSMenu *main = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appRoot = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"FE8 Extended Frontend"];
    NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:@"About FE8 Extended Frontend"
        action:@selector(showAbout:) keyEquivalent:@""];
    about.target = self;
    [appMenu addItem:about];
    [appMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit FE8 Extended Frontend"
        action:@selector(terminate:) keyEquivalent:@"q"];
    [appMenu addItem:quit];
    appRoot.submenu = appMenu;
    [main addItem:appRoot];

    NSMenuItem *fileRoot = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *add = [[NSMenuItem alloc] initWithTitle:@"Add ROMs…"
        action:@selector(addRoms:) keyEquivalent:@"o"];
    add.target = self;
    [fileMenu addItem:add];
    [fileMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *play = [[NSMenuItem alloc] initWithTitle:@"Play"
        action:@selector(play:) keyEquivalent:@""];
    play.target = self;
    [fileMenu addItem:play];
    NSMenuItem *resume = [[NSMenuItem alloc] initWithTitle:@"Resume State"
        action:@selector(resume:) keyEquivalent:@""];
    resume.target = self;
    [fileMenu addItem:resume];
    [fileMenu addItem:NSMenuItem.separatorItem];
    NSMenuItem *showRom = [[NSMenuItem alloc] initWithTitle:@"Show or Locate ROM"
        action:@selector(revealOrLocateRom:) keyEquivalent:@""];
    showRom.target = self;
    [fileMenu addItem:showRom];
    NSMenuItem *showSaves = [[NSMenuItem alloc] initWithTitle:@"Show Saves"
        action:@selector(revealSaves:) keyEquivalent:@""];
    showSaves.target = self;
    [fileMenu addItem:showSaves];
    NSMenuItem *remove = [[NSMenuItem alloc] initWithTitle:@"Remove from Library"
        action:@selector(remove:) keyEquivalent:@""];
    remove.target = self;
    [fileMenu addItem:remove];
    fileRoot.submenu = fileMenu;
    [main addItem:fileRoot];
    NSApp.mainMenu = main;
}

- (void)show {
    [self installMenu];
    if (self.games.count && self.table.selectedRow < 0)
        [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
            byExtendingSelection:NO];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [self.table reloadData];
    [self updateControls];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return self.games.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column
    row:(NSInteger)row {
    NSString *identifier = column.identifier;
    NSTextField *field = [tableView makeViewWithIdentifier:identifier owner:self];
    if (!field) {
        field = [NSTextField labelWithString:@""];
        field.identifier = identifier;
        field.lineBreakMode = NSLineBreakByTruncatingMiddle;
    }
    field.textColor = NSColor.labelColor;
    field.font = [NSFont systemFontOfSize:13];
    field.toolTip = nil;

    NSDictionary *game = self.games[row];
    if ([identifier isEqualToString:@"title"]) {
        field.stringValue = game[@"title"] ? game[@"title"] : @"Untitled";
        field.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
        field.toolTip = game[@"path"];
    } else if ([identifier isEqualToString:@"code"]) {
        NSString *internal = game[@"internalTitle"] ? game[@"internalTitle"] : @"";
        NSString *code = game[@"gameCode"] ? game[@"gameCode"] : @"";
        field.stringValue = code.length ? [NSString stringWithFormat:@"%@ · %@", internal, code] : internal;
        field.toolTip = [NSString stringWithFormat:@"Maker %@ · revision %@",
            game[@"makerCode"] ? game[@"makerCode"] : @"--",
            game[@"revision"] ? game[@"revision"] : @0];
    } else if ([identifier isEqualToString:@"compatibility"]) {
        NSString *compatibility = game[@"compatibility"] ? game[@"compatibility"] : @"Unknown";
        NSString *warnings = game[@"validationWarnings"];
        field.stringValue = warnings.length ?
            [@"Warning · " stringByAppendingString:compatibility] : compatibility;
        if (warnings.length) {
            field.textColor = NSColor.systemOrangeColor;
            field.toolTip = warnings;
        }
    } else if ([identifier isEqualToString:@"path"]) {
        NSString *path = game[@"path"] ? game[@"path"] : @"";
        field.stringValue = path.stringByAbbreviatingWithTildeInPath;
        field.toolTip = path;
    } else if ([identifier isEqualToString:@"last"]) {
        NSDate *lastPlayed = [game[@"lastPlayed"] isKindOfClass:NSDate.class] ?
            game[@"lastPlayed"] : nil;
        field.stringValue = lastPlayed ? [self.dateFormatter stringFromDate:lastPlayed] : @"Never";
    } else if ([identifier isEqualToString:@"save"]) {
        NSString *status = [self statusForGame:game];
        field.stringValue = status;
        if ([status isEqualToString:@"ROM missing"] ||
                [status isEqualToString:@"ROM unreadable"])
            field.textColor = NSColor.systemRedColor;
        else if ([status isEqualToString:@"Running"])
            field.textColor = NSColor.controlAccentColor;
    }
    return field;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    (void)notification;
    [self updateControls];
}

- (NSMutableDictionary *)selectedGame {
    NSInteger row = self.table.selectedRow;
    return row >= 0 && row < (NSInteger)self.games.count ? self.games[row] : nil;
}

- (NSMutableDictionary *)gameWithIdentifier:(NSString *)identifier {
    for (NSMutableDictionary *game in self.games) {
        if ([game[@"id"] isEqualToString:identifier])
            return game;
    }
    return nil;
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

- (NSString *)runLogPathForGame:(NSDictionary *)game {
    return [[self directoryForGame:game] stringByAppendingPathComponent:@"last-run.log"];
}

- (BOOL)migrateAdjacentSaveForGame:(NSDictionary *)game error:(NSError **)error {
    NSString *destination = [self savePathForGame:game];
    if ([NSFileManager.defaultManager fileExistsAtPath:destination])
        return YES;
    NSString *rom = game[@"path"];
    NSString *adjacent = [[rom stringByDeletingPathExtension]
        stringByAppendingPathExtension:@"sav"];
    if (![NSFileManager.defaultManager fileExistsAtPath:adjacent])
        return YES;
    return [NSFileManager.defaultManager copyItemAtPath:adjacent
        toPath:destination error:error];
}

- (BOOL)romIsReadableForGame:(NSDictionary *)game {
    BOOL directory = NO;
    NSString *path = game[@"path"];
    return path.length && [NSFileManager.defaultManager fileExistsAtPath:path
        isDirectory:&directory] && !directory &&
        [NSFileManager.defaultManager isReadableFileAtPath:path];
}

- (NSString *)statusForGame:(NSDictionary *)game {
    if ([self.runningGameIdentifiers containsObject:game[@"id"]])
        return @"Running";
    BOOL directory = NO;
    NSString *path = game[@"path"];
    if (!path.length || ![NSFileManager.defaultManager fileExistsAtPath:path
            isDirectory:&directory] || directory)
        return @"ROM missing";
    if (![NSFileManager.defaultManager isReadableFileAtPath:path])
        return @"ROM unreadable";
    if ([NSFileManager.defaultManager fileExistsAtPath:[self statePathForGame:game]])
        return @"Quick state";
    if ([NSFileManager.defaultManager fileExistsAtPath:[self savePathForGame:game]])
        return @"Cartridge save";
    return @"Ready";
}

- (void)updateControls {
    NSDictionary *game = [self selectedGame];
    BOOL selected = game != nil;
    BOOL readable = selected && [self romIsReadableForGame:game];
    BOOL running = selected && [self.runningGameIdentifiers containsObject:game[@"id"]];
    self.playButton.enabled = readable && !running;
    self.resumeButton.enabled = readable && !running &&
        [NSFileManager.defaultManager fileExistsAtPath:[self statePathForGame:game]];
    self.removeButton.enabled = selected && !running;
    self.revealRomButton.enabled = selected;
    self.revealRomButton.title = readable ? @"Show ROM" : @"Locate ROM…";
    self.revealButton.enabled = selected;
    self.emptyLabel.hidden = self.games.count != 0;
    if (self.runningGameIdentifiers.count) {
        self.statusLabel.stringValue = [NSString stringWithFormat:@"%lu %@ · %lu running",
            (unsigned long)self.games.count,
            self.games.count == 1 ? @"game" : @"games",
            (unsigned long)self.runningGameIdentifiers.count];
    } else {
        self.statusLabel.stringValue = [NSString stringWithFormat:@"%lu %@",
            (unsigned long)self.games.count,
            self.games.count == 1 ? @"game" : @"games"];
    }
}

- (void)saveLibrary {
    [NSUserDefaults.standardUserDefaults setObject:self.games forKey:kLibraryDefaultsKey];
}

- (void)addRoms:(id)sender {
    (void)sender;
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.title = @"Add Game Boy Advance ROMs";
    panel.prompt = @"Add to Library";
    panel.allowsMultipleSelection = YES;
    panel.canChooseDirectories = NO;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"gba"]];
    if ([panel runModal] == NSModalResponseOK) {
        NSMutableArray<NSString *> *paths = [NSMutableArray array];
        for (NSURL *url in panel.URLs)
            [paths addObject:url.path];
        [self importPaths:paths];
    }
}

- (void)showImportSummaryAdded:(NSUInteger)added updated:(NSUInteger)updated
    warnings:(NSArray<NSString *> *)warnings errors:(NSArray<NSString *> *)errors {
    if (!warnings.count && !errors.count)
        return;
    NSMutableArray<NSString *> *sections = [NSMutableArray array];
    if (added || updated)
        [sections addObject:[NSString stringWithFormat:@"Added %lu · Updated %lu",
            (unsigned long)added, (unsigned long)updated]];
    if (warnings.count)
        [sections addObject:[@"Warnings:\n" stringByAppendingString:
            abbreviatedList(warnings, 6)]];
    if (errors.count)
        [sections addObject:[@"Not imported:\n" stringByAppendingString:
            abbreviatedList(errors, 6)]];
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = errors.count ? @"Some ROMs were not imported" :
        @"ROMs imported with warnings";
    alert.informativeText = [sections componentsJoinedByString:@"\n\n"];
    alert.alertStyle = errors.count ? NSAlertStyleWarning : NSAlertStyleInformational;
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)importPaths:(NSArray<NSString *> *)paths {
    NSString *lastImportedIdentifier = nil;
    NSMutableArray<NSString *> *warnings = [NSMutableArray array];
    NSMutableArray<NSString *> *errors = [NSMutableArray array];
    NSUInteger added = 0;
    NSUInteger updated = 0;

    for (NSString *rawPath in paths) {
        NSString *path = rawPath.stringByStandardizingPath;
        NSString *name = path.lastPathComponent.length ? path.lastPathComponent : path;
        if ([path.pathExtension caseInsensitiveCompare:@"gba"] != NSOrderedSame) {
            [errors addObject:[NSString stringWithFormat:@"%@: expected a .gba file", name]];
            continue;
        }
        NSString *errorMessage = nil;
        NSString *warningMessage = nil;
        NSMutableDictionary *game = gameForPath(path, &errorMessage, &warningMessage);
        if (!game) {
            [errors addObject:[NSString stringWithFormat:@"%@: %@", name,
                errorMessage ? errorMessage : @"Unable to read ROM metadata"]];
            continue;
        }
        NSUInteger duplicate = [self.games indexOfObjectPassingTest:
            ^BOOL(NSDictionary *candidate, NSUInteger index, BOOL *stop) {
                (void)index;
                (void)stop;
                return [candidate[@"id"] isEqualToString:game[@"id"]];
            }];
        if (duplicate == NSNotFound) {
            [self.games addObject:game];
            ++added;
        } else {
            id lastPlayed = self.games[duplicate][@"lastPlayed"];
            if (lastPlayed)
                game[@"lastPlayed"] = lastPlayed;
            self.games[duplicate] = game;
            ++updated;
        }
        if (warningMessage.length)
            [warnings addObject:[NSString stringWithFormat:@"%@: %@", name, warningMessage]];
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
        if (selected != NSNotFound)
            [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:selected]
                byExtendingSelection:NO];
    }
    [self updateControls];
    [self showImportSummaryAdded:added updated:updated warnings:warnings errors:errors];
}

- (void)remove:(id)sender {
    (void)sender;
    NSInteger row = self.table.selectedRow;
    if (row < 0 || row >= (NSInteger)self.games.count)
        return;
    NSDictionary *game = self.games[row];
    if ([self.runningGameIdentifiers containsObject:game[@"id"]])
        return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = [NSString stringWithFormat:@"Remove “%@” from the library?",
        game[@"title"] ? game[@"title"] : @"this game"];
    alert.informativeText = @"The ROM, cartridge save, quick state, and run log will stay on disk.";
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn)
        return;

    [self.games removeObjectAtIndex:row];
    [self saveLibrary];
    [self.table reloadData];
    if (self.games.count) {
        NSInteger next = MIN(row, (NSInteger)self.games.count - 1);
        [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:next]
            byExtendingSelection:NO];
    }
    [self updateControls];
}

- (void)locateRomForGame:(NSMutableDictionary *)game {
    if (!game)
        return;
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.title = @"Locate Game Boy Advance ROM";
    panel.prompt = @"Use This ROM";
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"gba"]];
    if ([panel runModal] != NSModalResponseOK)
        return;

    NSString *errorMessage = nil;
    NSString *warningMessage = nil;
    NSMutableDictionary *replacement = gameForPath(
        panel.URL.path, &errorMessage, &warningMessage);
    if (!replacement) {
        [self showErrorWithTitle:@"Unable to use that ROM" message:errorMessage];
        return;
    }
    if (![replacement[@"id"] isEqualToString:game[@"id"]]) {
        [self showErrorWithTitle:@"That is a different ROM"
            message:@"The selected file has a different SHA-1. Add it as a separate library entry instead so its saves remain isolated."];
        return;
    }

    id lastPlayed = game[@"lastPlayed"];
    if (lastPlayed)
        replacement[@"lastPlayed"] = lastPlayed;
    NSUInteger index = [self.games indexOfObjectIdenticalTo:game];
    if (index != NSNotFound)
        self.games[index] = replacement;
    [self saveLibrary];
    [self.table reloadData];
    [self updateControls];
    if (warningMessage.length) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"ROM located with a header warning";
        alert.informativeText = warningMessage;
        alert.alertStyle = NSAlertStyleWarning;
        [alert beginSheetModalForWindow:self.window completionHandler:nil];
    }
}

- (void)revealOrLocateRom:(id)sender {
    (void)sender;
    NSMutableDictionary *game = [self selectedGame];
    if (!game)
        return;
    if (![self romIsReadableForGame:game]) {
        [self locateRomForGame:game];
        return;
    }
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
        @[[NSURL fileURLWithPath:game[@"path"]]]];
}

- (void)revealSaves:(id)sender {
    (void)sender;
    NSDictionary *game = [self selectedGame];
    if (!game)
        return;
    NSString *directory = [self directoryForGame:game];
    NSError *error = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:directory
            withIntermediateDirectories:YES attributes:nil error:&error]) {
        [self showErrorWithTitle:@"Unable to open save folder"
            message:error.localizedDescription];
        return;
    }
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
        @[[NSURL fileURLWithPath:directory isDirectory:YES]]];
}

- (void)showRunFailureForGame:(NSDictionary *)game status:(int)status
    logPath:(NSString *)logPath {
    NSString *log = tailOfTextFile(logPath, 8192);
    NSString *message = [NSString stringWithFormat:
        @"%@ exited with status %d. The latest diagnostic log is stored at:\n\n%@",
        game[@"title"] ? game[@"title"] : @"The game", status,
        logPath.stringByAbbreviatingWithTildeInPath];
    if (log.length)
        message = [message stringByAppendingFormat:@"\n\nLast log output:\n%@", log];
    [self showErrorWithTitle:@"Game exited unexpectedly" message:message];
}

- (void)launchGame:(NSDictionary *)game resume:(BOOL)resume {
    if (!game)
        return;
    if ([self.runningGameIdentifiers containsObject:game[@"id"]])
        return;
    if (![self romIsReadableForGame:game]) {
        [self showErrorWithTitle:@"ROM unavailable"
            message:@"The ROM is missing or unreadable. Use Locate ROM to reconnect the original file by SHA-1."];
        return;
    }

    NSString *validationError = nil;
    NSMutableDictionary *current = gameForPath(game[@"path"], &validationError, NULL);
    if (!current) {
        [self showErrorWithTitle:@"ROM validation failed" message:validationError];
        return;
    }
    if (![current[@"id"] isEqualToString:game[@"id"]]) {
        [self showErrorWithTitle:@"ROM changed since import"
            message:@"The file at this location no longer has the imported ROM's SHA-1. Add the changed file separately so it receives separate save storage."];
        return;
    }

    NSString *directory = [self directoryForGame:game];
    NSError *directoryError = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:directory
         withIntermediateDirectories:YES attributes:nil error:&directoryError]) {
        [self showErrorWithTitle:@"Unable to create game storage"
            message:directoryError.localizedDescription];
        return;
    }
    NSError *migrationError = nil;
    if (![self migrateAdjacentSaveForGame:game error:&migrationError]) {
        [self showErrorWithTitle:@"Unable to import adjacent save"
            message:migrationError.localizedDescription];
        return;
    }

    NSString *state = [self statePathForGame:game];
    NSMutableArray<NSString *> *arguments = [@[
        @"--rom", game[@"path"],
        @"--save", [self savePathForGame:game],
        @"--quick-state", state,
    ] mutableCopy];
    if (resume && [NSFileManager.defaultManager fileExistsAtPath:state])
        [arguments addObjectsFromArray:@[@"--state", state]];

    NSString *logPath = [self runLogPathForGame:game];
    [NSFileManager.defaultManager createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *logHandle = [NSFileHandle fileHandleForWritingAtPath:logPath];
    [logHandle truncateFileAtOffset:0];

    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:self.executablePath];
    task.arguments = arguments;
    task.standardInput = NSFileHandle.fileHandleWithNullDevice;
    task.standardOutput = logHandle ? logHandle : NSFileHandle.fileHandleWithNullDevice;
    task.standardError = logHandle ? logHandle : NSFileHandle.fileHandleWithNullDevice;
    NSString *identifier = [game[@"id"] copy];
    NSDictionary *gameSnapshot = [game copy];
    task.terminationHandler = ^(NSTask *finished) {
        [logHandle closeFile];
        int status = finished.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.runningTasks removeObject:finished];
            [self.runningGameIdentifiers removeObject:identifier];
            [self.table reloadData];
            [self updateControls];
            if (status != 0)
                [self showRunFailureForGame:gameSnapshot status:status logPath:logPath];
        });
    };

    NSError *launchError = nil;
    if (![task launchAndReturnError:&launchError]) {
        [logHandle closeFile];
        [self showErrorWithTitle:@"Unable to launch game"
            message:launchError.localizedDescription];
        return;
    }
    [self.runningTasks addObject:task];
    [self.runningGameIdentifiers addObject:identifier];
    NSMutableDictionary *storedGame = [self gameWithIdentifier:identifier];
    storedGame[@"lastPlayed"] = NSDate.date;
    [self saveLibrary];
    [self.table reloadData];
    [self updateControls];

    pid_t processIdentifier = task.processIdentifier;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            NSRunningApplication *running = [NSRunningApplication
                runningApplicationWithProcessIdentifier:processIdentifier];
            [running activateWithOptions:0];
        });
}

- (void)play:(id)sender {
    (void)sender;
    [self launchGame:[self selectedGame] resume:NO];
}

- (void)resume:(id)sender {
    (void)sender;
    [self launchGame:[self selectedGame] resume:YES];
}

- (void)showAbout:(id)sender {
    (void)sender;
    NSString *version = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"];
    if (!version.length)
        version = @"0.0.1";
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName: @"FE8 Extended Frontend",
        NSAboutPanelOptionVersion: [@"Version " stringByAppendingString:version],
        NSAboutPanelOptionCopyright:
            @"Original frontend code under the MIT License.",
        NSAboutPanelOptionCredits: aboutCredits(),
    }];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    if (menuItem.action == @selector(play:))
        return self.playButton.enabled;
    if (menuItem.action == @selector(resume:))
        return self.resumeButton.enabled;
    if (menuItem.action == @selector(remove:))
        return self.removeButton.enabled;
    if (menuItem.action == @selector(revealOrLocateRom:))
        return self.revealRomButton.enabled;
    if (menuItem.action == @selector(revealSaves:))
        return self.revealButton.enabled;
    return YES;
}

- (void)showErrorWithTitle:(NSString *)title message:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title.length ? title : @"FE8 Extended Frontend";
    alert.informativeText = message.length ? message : @"An unknown error occurred.";
    alert.alertStyle = NSAlertStyleWarning;
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

@end

static Fe8LibraryController *libraryController;
static Fe8HostSettings librarySettings;

int fe8_macos_run_library(const char *executable_path) {
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
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
