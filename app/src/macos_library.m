#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "host_settings.h"
#include "macos_library.h"
#include "macos_settings.h"

static NSString *const kLibraryDefaultsKey = @"FE8GameLibrary";

@class Fe8LibraryController;

@interface Fe8LibraryDropView : NSView <NSDraggingDestination>
@property(nonatomic, assign) Fe8LibraryController *controller;
@end

@interface Fe8LibraryController : NSObject <NSApplicationDelegate,
    NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, copy) NSString *executablePath;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTableView *table;
@property(nonatomic, strong) NSTextField *emptyLabel;
@property(nonatomic, strong) NSButton *playButton;
@property(nonatomic, strong) NSButton *resumeButton;
@property(nonatomic, strong) NSButton *removeButton;
@property(nonatomic, strong) NSButton *revealButton;
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *games;
@property(nonatomic, strong) NSMutableArray<NSTask *> *runningTasks;
- (instancetype)initWithExecutablePath:(NSString *)path;
- (void)importPaths:(NSArray<NSString *> *)paths;
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

@implementation Fe8LibraryDropView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        self.wantsLayer = YES;
    }
    return self;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.layer.backgroundColor = [NSColor.controlAccentColor colorWithAlphaComponent:0.10].CGColor;
    return NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    (void)sender;
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    self.layer.backgroundColor = NSColor.clearColor.CGColor;
    NSArray<NSURL *> *urls = [sender.draggingPasteboard readObjectsForClasses:@[NSURL.class]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    NSMutableArray<NSString *> *paths = [NSMutableArray array];
    for (NSURL *url in urls)
        if ([url.pathExtension caseInsensitiveCompare:@"gba"] == NSOrderedSame)
            [paths addObject:url.path];
    [self.controller importPaths:paths];
    return paths.count > 0;
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
    NSArray *stored = [NSUserDefaults.standardUserDefaults arrayForKey:kLibraryDefaultsKey];
    self.games = [NSMutableArray array];
    for (NSDictionary *game in stored)
        if ([game[@"id"] isKindOfClass:NSString.class] &&
                [game[@"path"] isKindOfClass:NSString.class])
            [self.games addObject:[game mutableCopy]];
    [self buildWindow];
    return self;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(0, 0, 940, 580);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"FE8 Library";
    self.window.minSize = NSMakeSize(720, 440);
    self.window.releasedWhenClosed = NO;

    Fe8LibraryDropView *content = [[Fe8LibraryDropView alloc] initWithFrame:frame];
    content.controller = self;
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.window.contentView = content;

    NSTextField *title = [NSTextField labelWithString:@"Game Library"];
    title.frame = NSMakeRect(28, 522, 500, 34);
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.autoresizingMask = NSViewMinYMargin;
    [content addSubview:title];

    NSTextField *subtitle = [NSTextField labelWithString:
        @"Drop Game Boy Advance ROMs here, or add them from disk."];
    subtitle.frame = NSMakeRect(30, 496, 600, 22);
    subtitle.textColor = NSColor.secondaryLabelColor;
    subtitle.autoresizingMask = NSViewMinYMargin;
    [content addSubview:subtitle];

    NSButton *add = [NSButton buttonWithTitle:@"Add ROMs…" target:self action:@selector(addRoms:)];
    add.frame = NSMakeRect(790, 514, 122, 32);
    add.bezelStyle = NSBezelStyleRounded;
    add.keyEquivalent = @"o";
    add.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    add.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    [content addSubview:add];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(24, 82, 892, 400)];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.drawsBackground = YES;

    self.table = [[NSTableView alloc] initWithFrame:scroll.bounds];
    self.table.dataSource = self;
    self.table.delegate = self;
    self.table.rowHeight = 34;
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.allowsEmptySelection = YES;
    self.table.doubleAction = @selector(play:);
    self.table.target = self;
    NSArray *columns = @[
        @[@"title", @"Title", @260],
        @[@"code", @"Game", @120],
        @[@"path", @"Location", @330],
        @[@"save", @"Progress", @150],
    ];
    for (NSArray *definition in columns) {
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:definition[0]];
        column.title = definition[1];
        column.width = [definition[2] doubleValue];
        column.minWidth = 80;
        [self.table addTableColumn:column];
    }
    scroll.documentView = self.table;
    [content addSubview:scroll];

    self.emptyLabel = [NSTextField labelWithString:
        @"No games imported yet\n\nDrop one or more .gba files anywhere in this window"];
    self.emptyLabel.frame = NSMakeRect(180, 245, 580, 74);
    self.emptyLabel.alignment = NSTextAlignmentCenter;
    self.emptyLabel.font = [NSFont systemFontOfSize:16 weight:NSFontWeightMedium];
    self.emptyLabel.textColor = NSColor.secondaryLabelColor;
    self.emptyLabel.maximumNumberOfLines = 3;
    self.emptyLabel.autoresizingMask = NSViewMinXMargin | NSViewMaxXMargin |
        NSViewMinYMargin | NSViewMaxYMargin;
    [content addSubview:self.emptyLabel];

    self.removeButton = [NSButton buttonWithTitle:@"Remove" target:self action:@selector(remove:)];
    self.removeButton.frame = NSMakeRect(24, 26, 92, 32);
    self.removeButton.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    self.removeButton.toolTip = @"Remove from the library without deleting saves";
    [content addSubview:self.removeButton];

    self.revealButton = [NSButton buttonWithTitle:@"Show Saves" target:self action:@selector(revealSaves:)];
    self.revealButton.frame = NSMakeRect(126, 26, 110, 32);
    self.revealButton.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    [content addSubview:self.revealButton];

    self.resumeButton = [NSButton buttonWithTitle:@"Resume State" target:self action:@selector(resume:)];
    self.resumeButton.frame = NSMakeRect(666, 26, 120, 32);
    self.resumeButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [content addSubview:self.resumeButton];

    self.playButton = [NSButton buttonWithTitle:@"Play" target:self action:@selector(play:)];
    self.playButton.frame = NSMakeRect(796, 26, 120, 32);
    self.playButton.bezelStyle = NSBezelStyleRounded;
    self.playButton.keyEquivalent = @"\r";
    self.playButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [content addSubview:self.playButton];

    [self updateControls];
    [self.window center];
}

- (void)installMenu {
    NSMenu *main = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *appRoot = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"FE8 Library"];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit FE8 Library"
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
    fileRoot.submenu = fileMenu;
    [main addItem:fileRoot];
    NSApp.mainMenu = main;
}

- (void)show {
    [self installMenu];
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
    NSDictionary *game = self.games[row];
    if ([identifier isEqualToString:@"title"])
        field.stringValue = game[@"title"] ? game[@"title"] : @"Untitled";
    else if ([identifier isEqualToString:@"code"]) {
        NSString *internal = game[@"internalTitle"] ? game[@"internalTitle"] : @"";
        NSString *code = game[@"gameCode"] ? game[@"gameCode"] : @"";
        field.stringValue = code.length ? [NSString stringWithFormat:@"%@ · %@", internal, code] : internal;
    } else if ([identifier isEqualToString:@"path"])
        field.stringValue = game[@"path"] ? game[@"path"] : @"";
    else if ([identifier isEqualToString:@"save"])
        field.stringValue = [self statusForGame:game];
    return field;
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    (void)notification;
    [self updateControls];
}

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

- (NSString *)statusForGame:(NSDictionary *)game {
    NSFileManager *files = NSFileManager.defaultManager;
    if (![files fileExistsAtPath:game[@"path"]])
        return @"ROM missing";
    if ([files fileExistsAtPath:[self statePathForGame:game]])
        return @"State available";
    if ([files fileExistsAtPath:[self savePathForGame:game]])
        return @"Save data";
    return @"Not played";
}

- (void)updateControls {
    NSDictionary *game = [self selectedGame];
    BOOL selected = game != nil;
    BOOL romExists = selected && [NSFileManager.defaultManager fileExistsAtPath:game[@"path"]];
    self.playButton.enabled = romExists;
    self.resumeButton.enabled = romExists &&
        [NSFileManager.defaultManager fileExistsAtPath:[self statePathForGame:game]];
    self.removeButton.enabled = selected;
    self.revealButton.enabled = selected;
    self.emptyLabel.hidden = self.games.count != 0;
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
        if (selected != NSNotFound)
            [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:selected]
                byExtendingSelection:NO];
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
            [self.table reloadData];
            [self updateControls];
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
    [self launchGame:[self selectedGame] resume:NO];
}

- (void)resume:(id)sender {
    (void)sender;
    [self launchGame:[self selectedGame] resume:YES];
}

- (void)showError:(NSString *)message {
    NSAlert *alert = [[NSAlert alloc] init];
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
