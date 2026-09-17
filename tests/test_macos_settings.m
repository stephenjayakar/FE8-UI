/* Real AppKit events and controls, with an isolated preference suite.
 * Include the implementation to test private event conversion and argv wiring;
 * no test-only hooks or alternate keyboard handler are added to the app. */
#include <assert.h>
#include <stdio.h>
#include <objc/runtime.h>
#include <SDL_syswm.h>
#include "../app/src/macos_settings.m"
#include "../app/src/macos_library.m"

static NSUserDefaults *isolated;
static id isolatedDefaults(id object, SEL selector) {
    (void)object; (void)selector; return isolated;
}
static NSEvent *keyEvent(NSWindow *window, unsigned short code, NSString *text) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil
        characters:text charactersIgnoringModifiers:text isARepeat:NO keyCode:code];
}
static void pump(BOOL sdl) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:0.2];
    do {
        if (sdl) {
            SDL_Event event; while (SDL_PollEvent(&event)) {}
        } else {
            NSEvent *event;
            while ((event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:NSDate.distantPast
                    inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:event];
        }
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    } while (end.timeIntervalSinceNow>0);
}
static void capture(NSWindow *window, NSString *directory, NSString *name) {
    if (!directory) return;
    [window display];
    NSView *view=window.contentView;
    NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    assert(image);
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    NSData *png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    assert([png writeToFile:[directory stringByAppendingPathComponent:name] atomically:YES]);
}
static void bindKey(Fe8SettingsController *controller, NSButton *button,
        unsigned short code, NSString *text, BOOL sdl) {
    [controller.window makeKeyAndOrderFront:nil];pump(sdl);
    [button.superview scrollRectToVisible:button.frame];
    [button performClick:nil];
    assert(controller.listeningButton==button);
    [NSApp postEvent:keyEvent(controller.window,code,text) atStart:NO];
    pump(sdl);
    assert(!controller.listeningButton && !controller.keyMonitor);
}
int main(int argc, char **argv) {
    @autoreleasepool {
        assert(SDL_WasInit(0)==0);
        /* Older SDL builds leave this keymap empty; newer ones supply a
         * default. Binding must work before video initialization in either case. */
        printf("Pre-init SDL letter lookup: %d\n", SDL_GetScancodeFromKey(SDLK_v));
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        NSString *suite=[@"org.fe8.tests.settings." stringByAppendingString:NSUUID.UUID.UUIDString];
        isolated=[[NSUserDefaults alloc] initWithSuiteName:suite];
        Method method=class_getClassMethod(NSUserDefaults.class,@selector(standardUserDefaults));
        IMP original=method_setImplementation(method,(IMP)isolatedDefaults);
        Fe8HostSettings settings;fe8_host_settings_init(&settings);
        fe8_macos_load_settings(&settings);
        fe8_macos_install_settings_menu(&settings,NULL,NULL,NULL,NULL);
        Fe8SettingsController *controller=settingsController;
        [controller showSettings:nil];pump(NO);
        assert(controller.bindingButtons.count==FE8_HOST_BUTTON_COUNT+FE8_HOST_HOTKEY_COUNT);
        NSButton *button=controller.bindingButtons[FE8_HOST_BUTTON_COUNT+FE8_HOST_HOTKEY_TOGGLE_VOXEL];
        for (NSButton *binding in controller.bindingButtons)
            assert(NSContainsRect(binding.superview.bounds,binding.frame));
        /* Cocoa virtual positions, not typed characters or a not-yet-live SDL keymap. */
        assert(scancodeForEvent(keyEvent(controller.window,9,@"v"))==SDL_SCANCODE_V);
        assert(scancodeForEvent(keyEvent(controller.window,9,@"V"))==SDL_SCANCODE_V);
        assert(scancodeForEvent(keyEvent(controller.window,9,@"ø"))==SDL_SCANCODE_V);
        assert(scancodeForEvent(keyEvent(controller.window,18,@"1"))==SDL_SCANCODE_1);
        assert(scancodeForEvent(keyEvent(controller.window,76,@"\r"))==SDL_SCANCODE_KP_ENTER);
        assert(scancodeForEvent(keyEvent(controller.window,65535,@"v"))==SDL_SCANCODE_UNKNOWN);
        settings.hotkeys[FE8_HOST_HOTKEY_QUICK_SAVE]=SDL_SCANCODE_V;
        bindKey(controller,button,9,@"v",NO);
        assert([button.title isEqualToString:@"V"]);
        assert(settings.hotkeys[FE8_HOST_HOTKEY_TOGGLE_VOXEL]==SDL_SCANCODE_V);
        assert(settings.hotkeys[FE8_HOST_HOTKEY_QUICK_SAVE]==SDL_SCANCODE_UNKNOWN);
        assert(fe8_host_hotkey_for_scancode(&settings,SDL_SCANCODE_UNKNOWN)==0);
        Fe8HostSettings restored=settings;fe8_macos_load_settings(&restored);
        assert(restored.hotkeys[FE8_HOST_HOTKEY_TOGGLE_VOXEL]==SDL_SCANCODE_V);
        assert(fe8_host_hotkey_for_scancode(&restored,SDL_SCANCODE_V)==
            (UINT32_C(1)<<FE8_HOST_HOTKEY_TOGGLE_VOXEL));
        assert(SDL_WasInit(0)==0);
        puts("PASS actual Cocoa V binding, label, conflict removal and persistence before SDL_Init");
        NSString *directory=argc>1?[NSString stringWithUTF8String:argv[1]]:nil;
        if (directory) [NSFileManager.defaultManager createDirectoryAtPath:directory
            withIntermediateDirectories:YES attributes:nil error:nil];
        capture(controller.window,directory,@"macos-voxel-binding-v.png");
        [button performClick:nil];
        [NSApp postEvent:keyEvent(controller.window,53,@"\033") atStart:NO];pump(NO);
        assert(!controller.listeningButton && [button.title isEqualToString:@"V"]);
        [isolated setInteger:SDL_SCANCODE_B forKey:hotkeyBindingKey(FE8_HOST_HOTKEY_TOGGLE_VOXEL)];
        [controller refreshBindings:nil];
        assert(settings.hotkeys[FE8_HOST_HOTKEY_TOGGLE_VOXEL]==SDL_SCANCODE_B);
        bindKey(controller,button,9,@"v",NO);
        puts("PASS Escape cancellation and binding refresh from another process's saved preferences");
        assert(settings.voxel_gpu);
        [controller.voxelBackendPopup selectItemAtIndex:1];
        [controller voxelBackendChanged:controller.voxelBackendPopup];
        assert(!settings.voxel_gpu);
        fe8_macos_load_settings(&restored);assert(!restored.voxel_gpu);
        [controller.voxelBackendPopup selectItemAtIndex:0];
        [controller voxelBackendChanged:controller.voxelBackendPopup];
        fe8_macos_load_settings(&restored);assert(settings.voxel_gpu && restored.voxel_gpu);
        assert(NSContainsRect(controller.voxelBackendPopup.superview.bounds,controller.voxelBackendPopup.frame));
        puts("PASS native OpenGL/software selector and persistence");
        [controller.voxelBackendPopup.superview scrollRectToVisible:controller.voxelBackendPopup.frame];
        pump(NO);capture(controller.window,directory,@"macos-voxel-gpu-setting.png");
        assert(!settings.voxel_enabled);
        [controller.voxelButton performClick:nil];
        assert(settings.voxel_enabled);
        fe8_macos_load_settings(&restored);assert(restored.voxel_enabled);
        NSMenuItem *menu=[[[NSApp.mainMenu itemWithTitle:@"Settings"] submenu] itemWithTitle:@"Voxel Renderer"];
        assert(menu && [controller validateMenuItem:menu] && menu.state==NSControlStateValueOn);
        [NSApp sendAction:menu.action to:menu.target from:menu];
        assert(!settings.voxel_enabled && controller.voxelButton.state==NSControlStateValueOff);
        [controller.voxelButton performClick:nil];
        [controller.voxelButton.superview scrollRectToVisible:controller.voxelButton.frame];
        capture(controller.window,directory,@"macos-voxel-enabled.png");
        NSArray *normal=gameArguments(@"/rom with spaces.gba",@"/save.sav",@"/state.ss",NO,NO);
        NSArray *voxel=gameArguments(@"/rom with spaces.gba",@"/save.sav",@"/state.ss",YES,YES);
        assert(![normal containsObject:@"--voxel"] && [voxel containsObject:@"--voxel"]);
        assert([voxel containsObject:@"--state"] && [voxel containsObject:@"/rom with spaces.gba"]);
        puts("PASS checkbox/menu synchronization, persisted mode, and voxel Play/Resume argv");
        /* Repeat binding while SDL owns the Cocoa event pump, as in a running game. */
        assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0);
        SDL_Window *game=SDL_CreateWindow("Settings regression",0,0,640,480,0);assert(game);
        bindKey(controller,button,11,@"b",YES);
        assert(settings.hotkeys[FE8_HOST_HOTKEY_TOGGLE_VOXEL]==SDL_SCANCODE_B);
        bindKey(controller,button,9,@"v",YES);
        assert([button.title isEqualToString:@"V"]);
        puts("PASS actual Cocoa V rebinding through SDL_PollEvent after SDL video initialization");
        [controller.window close];SDL_DestroyWindow(game);SDL_Quit();
        [NSNotificationCenter.defaultCenter removeObserver:controller];
        method_setImplementation(method,original);
        [isolated removePersistentDomainForName:suite];
    }
    return 0;
}
