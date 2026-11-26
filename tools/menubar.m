// Clawd Mochi Native macOS Menu Bar Companion (tools/menubar.m)
// Compile: clang -O2 -fobjc-arc tools/menubar.m -o tools/ClawdMochiBar -framework Cocoa

#import <Cocoa/Cocoa.h>

static NSString *const kApiBase = @"http://127.0.0.1:18765";

@interface MochiPopoverVC : NSViewController <NSTextFieldDelegate>
@property (nonatomic, strong) NSTextField *statusLabel;
@property (nonatomic, strong) NSTextField *quipLabel;
@property (nonatomic, strong) NSTextField *feedbackLabel;
@property (nonatomic, strong) NSTextField *inputField;
@property (nonatomic, strong) NSPopUpButton *themePopup;
@property (nonatomic, strong) NSPopUpButton *hatPopup;
@property (nonatomic, strong) NSButton *soundCheck;
@property (nonatomic, strong) NSButton *ttsCheck;
@property (nonatomic, strong) NSButton *llmCheck;
@property (nonatomic, strong) NSButton *pomoBtn;
@property (nonatomic, assign) BOOL lastPomoActive;
@property (nonatomic, assign) BOOL suppressSync;
- (void)updateUI:(NSDictionary *)st;
@end

@implementation MochiPopoverVC

- (NSButton *)makeBtn:(NSString *)title frame:(NSRect)frame action:(SEL)action {
    NSButton *b = [[NSButton alloc] initWithFrame:frame];
    [b setTitle:title];
    [b setBezelStyle:NSBezelStyleRounded];
    [b setFont:[NSFont systemFontOfSize:12]];
    [b setTarget:self];
    [b setAction:action];
    return b;
}

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 336, 472)];
    self.view = root;
    CGFloat y = 438;

    NSTextField *title = [NSTextField labelWithString:@"🦀 Clawd Mochi 桌面小搭档"];
    title.font = [NSFont boldSystemFontOfSize:14];
    title.frame = NSMakeRect(16, y, 224, 20);
    [root addSubview:title];

    [root addSubview:[self makeBtn:@"网页版" frame:NSMakeRect(252, y - 3, 68, 24) action:@selector(openWeb)]];
    y -= 24;

    self.statusLabel = [NSTextField labelWithString:@"正在连接桌面小螃蟹..."];
    self.statusLabel.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    self.statusLabel.textColor = [NSColor secondaryLabelColor];
    self.statusLabel.frame = NSMakeRect(16, y, 304, 18);
    [root addSubview:self.statusLabel];
    y -= 46;

    NSBox *quipBox = [[NSBox alloc] initWithFrame:NSMakeRect(16, y, 304, 42)];
    quipBox.boxType = NSBoxCustom;
    quipBox.cornerRadius = 8;
    quipBox.borderWidth = 1;
    quipBox.borderColor = [NSColor separatorColor];
    quipBox.fillColor = [NSColor controlBackgroundColor];
    self.quipLabel = [NSTextField wrappingLabelWithString:@"等待同步中..."];
    self.quipLabel.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
    self.quipLabel.frame = NSMakeRect(10, 6, 284, 30);
    [quipBox addSubview:self.quipLabel];
    [root addSubview:quipBox];
    y -= 24;

    NSTextField *feedTitle = [NSTextField labelWithString:@"投喂零食（真机吃东西动画 + 大模型吃货反馈）"];
    feedTitle.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    feedTitle.textColor = [NSColor secondaryLabelColor];
    feedTitle.frame = NSMakeRect(16, y, 304, 16);
    [root addSubview:feedTitle];
    y -= 32;

    [root addSubview:[self makeBtn:@"🐟 喂小鱼干 (+2)" frame:NSMakeRect(16, y, 148, 28) action:@selector(feedFish)]];
    [root addSubview:[self makeBtn:@"☕ 喂冰美式 (秒醒)" frame:NSMakeRect(172, y, 148, 28) action:@selector(feedCoffee)]];
    y -= 32;
    [root addSubview:[self makeBtn:@"🍓 草莓麻薯 (+4)" frame:NSMakeRect(16, y, 148, 28) action:@selector(feedMochi)]];
    [root addSubview:[self makeBtn:@"🍵 养生热茶 (放松)" frame:NSMakeRect(172, y, 148, 28) action:@selector(feedTea)]];
    y -= 36;

    NSTextField *actTitle = [NSTextField labelWithString:@"快捷互动"];
    actTitle.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    actTitle.textColor = [NSColor secondaryLabelColor];
    actTitle.frame = NSMakeRect(16, y + 14, 304, 16);
    [root addSubview:actTitle];

    [root addSubview:[self makeBtn:@"❤️ 摸摸头" frame:NSMakeRect(16, y - 16, 97, 28) action:@selector(doPet)]];
    self.pomoBtn = [self makeBtn:@"🍅 25m 专注" frame:NSMakeRect(119, y - 16, 98, 28) action:@selector(togglePomo)];
    [root addSubview:self.pomoBtn];
    [root addSubview:[self makeBtn:@"💬 小剧场" frame:NSMakeRect(223, y - 16, 97, 28) action:@selector(doTheater)]];
    y -= 44;

    NSTextField *chatTitle = [NSTextField labelWithString:@"隔空发弹幕 / 跟桌面小螃蟹聊一句"];
    chatTitle.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    chatTitle.textColor = [NSColor secondaryLabelColor];
    chatTitle.frame = NSMakeRect(16, y, 304, 16);
    [root addSubview:chatTitle];
    y -= 30;

    self.inputField = [[NSTextField alloc] initWithFrame:NSMakeRect(16, y, 304, 24)];
    self.inputField.placeholderString = @"输入文字，点发弹幕或让小螃蟹回复...";
    self.inputField.font = [NSFont systemFontOfSize:12];
    self.inputField.delegate = self;
    [root addSubview:self.inputField];
    y -= 32;

    [root addSubview:[self makeBtn:@"📢 发弹幕到屏幕" frame:NSMakeRect(16, y, 148, 27) action:@selector(sendDanmaku)]];
    NSButton *chatBtn = [self makeBtn:@"✨ 跟它聊一句 (AI)" frame:NSMakeRect(172, y, 148, 27) action:@selector(sendChat)];
    chatBtn.keyEquivalent = @"\r";
    [root addSubview:chatBtn];
    y -= 36;

    NSTextField *styleTitle = [NSTextField labelWithString:@"主题配色与头顶像素小配饰"];
    styleTitle.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    styleTitle.textColor = [NSColor secondaryLabelColor];
    styleTitle.frame = NSMakeRect(16, y + 12, 304, 16);
    [root addSubview:styleTitle];

    self.themePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, y - 16, 148, 26) pullsDown:NO];
    [self.themePopup addItemsWithTitles:@[@"主题: 自动换色", @"主题: 珊瑚橙", @"主题: 赛博青", @"主题: 樱花粉", @"主题: 暖金护眼", @"主题: 薄荷绿"]];
    self.themePopup.target = self;
    self.themePopup.action = @selector(themeChanged);
    [root addSubview:self.themePopup];

    self.hatPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(172, y - 16, 148, 26) pullsDown:NO];
    [self.hatPopup addItemsWithTitles:@[@"配饰: 自动穿戴", @"配饰: 纯净大眼", @"配饰: 金色皇冠", @"配饰: 赛博耳机", @"配饰: 像素睡帽", @"配饰: 必胜头带"]];
    self.hatPopup.target = self;
    self.hatPopup.action = @selector(hatChanged);
    [root addSubview:self.hatPopup];
    y -= 48;

    self.soundCheck = [NSButton checkboxWithTitle:@"8-bit 音效" target:self action:@selector(audioToggled)];
    self.soundCheck.frame = NSMakeRect(16, y, 96, 20);
    [root addSubview:self.soundCheck];

    self.ttsCheck = [NSButton checkboxWithTitle:@"语音朗读" target:self action:@selector(audioToggled)];
    self.ttsCheck.frame = NSMakeRect(118, y, 92, 20);
    [root addSubview:self.ttsCheck];

    self.llmCheck = [NSButton checkboxWithTitle:@"大模型台词" target:self action:@selector(audioToggled)];
    self.llmCheck.frame = NSMakeRect(216, y, 104, 20);
    [root addSubview:self.llmCheck];

    self.feedbackLabel = [NSTextField labelWithString:@"就绪 · 大模型动态台词已启用"];
    self.feedbackLabel.font = [NSFont systemFontOfSize:11];
    self.feedbackLabel.textColor = [NSColor secondaryLabelColor];
    self.feedbackLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    self.feedbackLabel.frame = NSMakeRect(16, 12, 240, 18);
    [root addSubview:self.feedbackLabel];

    [root addSubview:[self makeBtn:@"退出" frame:NSMakeRect(266, 8, 54, 24) action:@selector(quitApp)]];
}

- (void)updateUI:(NSDictionary *)st {
    if (!st || ![st[@"online"] boolValue]) {
        self.statusLabel.stringValue = @"未检测到桌面小螃蟹 (请检查 USB 连接)";
        return;
    }
    NSString *mood = st[@"mood"] ?: @"HAPPY";
    NSInteger lv = [st[@"lv"] integerValue];
    NSInteger love = [st[@"love"] integerValue];
    double temp = [st[@"temp"] doubleValue];
    BOOL pres = [st[@"present"] boolValue];
    NSString *repo = st[@"active_repo"] ?: @"clawpet";
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ · Lv.%ld(♥%ld) · %@ · %.1f°C · %@",
                                    mood, (long)lv, (long)love, pres ? @"在桌前" : @"离座", temp, repo];

    NSString *quip = st[@"quip"];
    if (quip.length > 0) {
        self.quipLabel.stringValue = [NSString stringWithFormat:@"💬 %@", quip];
    } else {
        NSInteger streak = [st[@"streak_min"] integerValue];
        self.quipLabel.stringValue = [NSString stringWithFormat:@"🦀 安静陪伴中... (连续工作 %ld 分钟)", (long)streak];
    }

    NSInteger pomo = [st[@"pomo"] integerValue];
    self.lastPomoActive = (pomo > 0);
    if (self.lastPomoActive) {
        self.pomoBtn.title = [NSString stringWithFormat:@"⏹ %ld:%02ld", (long)(pomo / 60), (long)(pomo % 60)];
    } else {
        self.pomoBtn.title = @"🍅 25m 专注";
    }

    self.suppressSync = YES;
    NSInteger th = [st[@"theme"] integerValue];
    if (th >= 0 && th < self.themePopup.numberOfItems) [self.themePopup selectItemAtIndex:th];
    NSInteger ht = [st[@"hat"] integerValue];
    if (ht >= 0 && ht < self.hatPopup.numberOfItems) [self.hatPopup selectItemAtIndex:ht];
    NSDictionary *au = st[@"audio"];
    if ([au isKindOfClass:[NSDictionary class]]) {
        self.soundCheck.state = [au[@"sound_enabled"] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
        self.ttsCheck.state = [au[@"tts_enabled"] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
        self.llmCheck.state = [au[@"llm_enabled"] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
    }
    self.suppressSync = NO;
}

- (void)postJSON:(NSString *)path body:(NSDictionary *)body doneText:(NSString *)doneText {
    NSURL *url = [NSURL URLWithString:[kApiBase stringByAppendingString:path]];
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.HTTPMethod = @"POST";
    [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    req.timeoutInterval = 8.0;
    req.HTTPBody = [NSJSONSerialization dataWithJSONObject:body options:0 error:nil];

    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (err) {
                self.feedbackLabel.stringValue = [NSString stringWithFormat:@"请求失败: %@", err.localizedDescription];
                return;
            }
            NSDictionary *obj = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
            NSString *reply = [obj isKindOfClass:[NSDictionary class]] ? obj[@"reply"] : nil;
            if ([reply isKindOfClass:[NSString class]] && reply.length > 0 && ![reply hasPrefix:@"OK:"]) {
                self.feedbackLabel.stringValue = [NSString stringWithFormat:@"小螃蟹: %@", reply];
                self.quipLabel.stringValue = [NSString stringWithFormat:@"💬 %@", reply];
            } else if (doneText) {
                self.feedbackLabel.stringValue = doneText;
            }
        });
    }] resume];
}

- (void)feedFood:(NSString *)food label:(NSString *)label {
    self.feedbackLabel.stringValue = [NSString stringWithFormat:@"正在投喂%@...", label];
    [self postJSON:@"/api/feed" body:@{@"food": food} doneText:[NSString stringWithFormat:@"已投喂%@!", label]];
}

- (void)feedFish   { [self feedFood:@"FISH"   label:@"小鱼干"]; }
- (void)feedCoffee { [self feedFood:@"COFFEE" label:@"冰美式"]; }
- (void)feedMochi  { [self feedFood:@"MOCHI"  label:@"草莓麻薯"]; }
- (void)feedTea    { [self feedFood:@"TEA"    label:@"养生热茶"]; }

- (void)doPet {
    self.feedbackLabel.stringValue = @"正在摸摸头...";
    [self postJSON:@"/api/cmd" body:@{@"cmd": @"MOCHI:PET"} doneText:@"摸摸头成功 (亲密度+1)"];
}

- (void)togglePomo {
    if (self.lastPomoActive) {
        [self postJSON:@"/api/cmd" body:@{@"cmd": @"MOCHI:POMO:STOP"} doneText:@"已停止番茄钟"];
    } else {
        [self postJSON:@"/api/cmd" body:@{@"cmd": @"MOCHI:POMO:25"} doneText:@"已开启 25 分钟番茄钟"];
    }
}

- (void)doTheater {
    [self postJSON:@"/api/cmd" body:@{@"cmd": @"MOCHI:THEATER"} doneText:@"真机正在播放小剧场~"];
}

- (void)sendDanmaku {
    NSString *t = [self.inputField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (t.length == 0) return;
    self.inputField.stringValue = @"";
    self.feedbackLabel.stringValue = @"弹幕已发往真机!";
    [self postJSON:@"/api/cmd" body:@{@"cmd": [@"MOCHI:SAY:" stringByAppendingString:t]} doneText:[NSString stringWithFormat:@"已上屏: %@", t]];
}

- (void)sendChat {
    NSString *t = [self.inputField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (t.length == 0) return;
    self.inputField.stringValue = @"";
    self.feedbackLabel.stringValue = @"小螃蟹正在思考怎么回你...";
    [self postJSON:@"/api/chat" body:@{@"text": t} doneText:nil];
}

- (void)themeChanged {
    if (self.suppressSync) return;
    NSInteger idx = self.themePopup.indexOfSelectedItem;
    [self postJSON:@"/api/cmd" body:@{@"cmd": [NSString stringWithFormat:@"MOCHI:THEME:%ld", (long)idx]} doneText:@"已切换眼睛主题色"];
}

- (void)hatChanged {
    if (self.suppressSync) return;
    NSInteger idx = self.hatPopup.indexOfSelectedItem;
    [self postJSON:@"/api/cmd" body:@{@"cmd": [NSString stringWithFormat:@"MOCHI:HAT:%ld", (long)idx]} doneText:@"已切换头顶像素配饰"];
}

- (void)audioToggled {
    if (self.suppressSync) return;
    [self postJSON:@"/api/audio" body:@{
        @"sound_enabled": @(self.soundCheck.state == NSControlStateValueOn),
        @"tts_enabled": @(self.ttsCheck.state == NSControlStateValueOn),
        @"llm_enabled": @(self.llmCheck.state == NSControlStateValueOn)
    } doneText:@"已更新声音与大模型设置"];
}

- (void)openWeb {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[kApiBase stringByAppendingString:@"/preview.html"]]];
}

- (void)quitApp {
    [NSApp terminate:nil];
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSStatusItem *statusItem;
@property (nonatomic, strong) NSPopover *popover;
@property (nonatomic, strong) MochiPopoverVC *vc;
@property (nonatomic, strong) NSTimer *timer;
@end

@implementation AppDelegate

- (void)ensureDaemonRunning {
    if ([[NSFileManager defaultManager] fileExistsAtPath:@"/tmp/clawd-mochi.sock"]) return;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSTask *task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/opt/homebrew/bin/python3"];
        task.arguments = @[@"/Users/yan/Desktop/clawpet/tools/mochi.py", @"status"];
        task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        task.standardError = [NSFileHandle fileHandleWithNullDevice];
        @try { [task launchAndReturnError:nil]; } @catch (NSException *e) {}
    });
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self ensureDaemonRunning];
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    self.statusItem.button.title = @"🦀";
    self.statusItem.button.target = self;
    self.statusItem.button.action = @selector(togglePopover:);

    self.vc = [[MochiPopoverVC alloc] init];
    self.popover = [[NSPopover alloc] init];
    self.popover.contentViewController = self.vc;
    self.popover.behavior = NSPopoverBehaviorTransient;

    [self pollStatus];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.5 target:self selector:@selector(pollStatus) userInfo:nil repeats:YES];

    // Briefly pop open once on launch so the user immediately sees where the 🦀 menu bar icon is
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (!self.popover.isShown && self.statusItem.button) {
            [self togglePopover:nil];
        }
    });
}

- (void)togglePopover:(id)sender {
    NSStatusBarButton *btn = self.statusItem.button;
    if (!btn) return;
    if (self.popover.isShown) {
        [self.popover performClose:sender];
    } else {
        [self pollStatus];
        [self.popover showRelativeToRect:btn.bounds ofView:btn preferredEdge:NSRectEdgeMinY];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

- (void)pollStatus {
    NSURL *url = [NSURL URLWithString:[kApiBase stringByAppendingString:@"/api/status"]];
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.timeoutInterval = 1.2;
    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
        NSDictionary *obj = (data && !err) ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateBarTitle:obj];
            [self.vc updateUI:obj];
        });
    }] resume];
}

- (void)updateBarTitle:(NSDictionary *)st {
    NSStatusBarButton *btn = self.statusItem.button;
    if (!btn) return;
    if (!st || ![st[@"online"] boolValue]) {
        btn.title = @"🦀";
        btn.toolTip = @"Clawd Mochi (离线)";
        return;
    }
    NSInteger pomo = [st[@"pomo"] integerValue];
    NSInteger ai = [st[@"ai"] integerValue];
    NSInteger lv = [st[@"lv"] integerValue];
    btn.toolTip = [NSString stringWithFormat:@"Clawd Mochi · Lv.%ld", (long)lv];
    if (pomo > 0) {
        btn.title = [NSString stringWithFormat:@"🦀%ld:%02ld", (long)(pomo / 60), (long)(pomo % 60)];
    } else if (ai > 0) {
        btn.title = @"🦀⚡";
    } else {
        btn.title = @"🦀";
    }
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
