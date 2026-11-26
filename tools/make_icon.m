#import <Cocoa/Cocoa.h>

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        CGFloat size = 1024.0;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:(NSInteger)size
                          pixelsHigh:(NSInteger)size
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];
        NSGraphicsContext *ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:ctx];

        // Clear transparent canvas
        [[NSColor clearColor] setFill];
        NSRectFill(NSMakeRect(0, 0, size, size));

        // 1. macOS Big Sur / Sequoia squircle card (100px margin -> 824x824)
        NSRect card = NSMakeRect(100, 100, 824, 824);
        NSBezierPath *squircle = [NSBezierPath bezierPathWithRoundedRect:card xRadius:185 yRadius:185];

        // Soft drop shadow
        NSShadow *shadow = [[NSShadow alloc] init];
        shadow.shadowColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.35];
        shadow.shadowOffset = NSMakeSize(0, -18);
        shadow.shadowBlurRadius = 32;
        [shadow set];
        [[NSColor colorWithCalibratedRed:0.12 green:0.10 blue:0.09 alpha:1.0] setFill];
        [squircle fill];

        // Reset shadow & draw warm dark charcoal gradient background inside squircle
        [NSGraphicsContext saveGraphicsState];
        [squircle addClip];
        NSGradient *bgGrad = [[NSGradient alloc] initWithColors:@[
            [NSColor colorWithCalibratedRed:0.19 green:0.15 blue:0.14 alpha:1.0],
            [NSColor colorWithCalibratedRed:0.11 green:0.09 blue:0.09 alpha:1.0]
        ]];
        [bgGrad drawInRect:card angle:-90.0];

        // Subtle coral ambient glow behind Clawd
        NSGradient *glow = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithCalibratedRed:0.92 green:0.35 blue:0.16 alpha:0.28]
                                                         endingColor:[NSColor colorWithCalibratedRed:0.92 green:0.35 blue:0.16 alpha:0.0]];
        [glow drawFromCenter:NSMakePoint(512, 500) radius:20 toCenter:NSMakePoint(512, 500) radius:380 options:0];
        [NSGraphicsContext restoreGraphicsState];

        // Subtle inner rim highlight
        [[NSColor colorWithCalibratedWhite:1.0 alpha:0.10] setStroke];
        squircle.lineWidth = 6.0;
        [squircle stroke];

        // 2. Iconic Pixel Clawd Creature (centered, crisp grid unit = 44px)
        // Grid: 13 cols x 10 rows (origin bottom-left of creature)
        CGFloat u = 44.0;
        CGFloat ox = 512.0 - (13.0 * u) / 2.0; // 226
        CGFloat oy = 250.0;

        NSColor *clawdTop = [NSColor colorWithCalibratedRed:0.95 green:0.42 blue:0.22 alpha:1.0];
        NSColor *clawdBot = [NSColor colorWithCalibratedRed:0.84 green:0.22 blue:0.08 alpha:1.0];
        NSGradient *bodyGrad = [[NSGradient alloc] initWithStartingColor:clawdTop endingColor:clawdBot];

        // Build Clawd pixel silhouette path
        NSBezierPath *clawd = [NSBezierPath bezierPath];
        // Main head/body block: cols 1..11 (width 11u), rows 2..8 (height 6u)
        [clawd appendBezierPathWithRoundedRect:NSMakeRect(ox + 1*u, oy + 2*u, 11*u, 6*u) xRadius:22 yRadius:22];
        // Left & right little side arms: col 0..1 & 12..13, rows 3.5..6
        [clawd appendBezierPathWithRoundedRect:NSMakeRect(ox + 0*u, oy + 3.6*u, 1.4*u, 2.3*u) xRadius:12 yRadius:12];
        [clawd appendBezierPathWithRoundedRect:NSMakeRect(ox + 11.6*u, oy + 3.6*u, 1.4*u, 2.3*u) xRadius:12 yRadius:12];
        // Four iconic Clawd legs at bottom: rows 0..2.3
        CGFloat legCols[4] = {1.7, 3.6, 8.2, 10.1};
        for (int i = 0; i < 4; i++) {
            [clawd appendBezierPathWithRoundedRect:NSMakeRect(ox + legCols[i]*u, oy + 0.1*u, 1.2*u, 2.3*u) xRadius:10 yRadius:10];
        }

        [NSGraphicsContext saveGraphicsState];
        [clawd addClip];
        [bodyGrad drawInRect:NSMakeRect(ox, oy, 13*u, 9*u) angle:-90.0];
        [NSGraphicsContext restoreGraphicsState];

        // 3. Iconic bold black rectangular eyes (matching Clawd Mochi's screen!)
        [[NSColor colorWithCalibratedRed:0.06 green:0.05 blue:0.05 alpha:1.0] setFill];
        NSBezierPath *eyeL = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(ox + 2.9*u, oy + 4.3*u, 1.25*u, 2.2*u) xRadius:8 yRadius:8];
        NSBezierPath *eyeR = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(ox + 8.85*u, oy + 4.3*u, 1.25*u, 2.2*u) xRadius:8 yRadius:8];
        [eyeL fill];
        [eyeR fill];

        // Tiny subtle white eye sparkle dots (cute desktop pet touch!)
        [[NSColor colorWithCalibratedWhite:1.0 alpha:0.92] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(ox + 3.15*u, oy + 5.75*u, 0.38*u, 0.38*u) xRadius:4 yRadius:4] fill];
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(ox + 9.10*u, oy + 5.75*u, 0.38*u, 0.38*u) xRadius:4 yRadius:4] fill];

        // 4. Mini golden pixel crown floating above Clawd's head (matches Lv.5+ Mochi!)
        CGFloat cx = 512.0, by = oy + 8.35*u;
        NSBezierPath *crown = [NSBezierPath bezierPath];
        [crown moveToPoint:NSMakePoint(cx - 76, by)];
        [crown lineToPoint:NSMakePoint(cx - 86, by + 76)];
        [crown lineToPoint:NSMakePoint(cx - 32, by + 36)];
        [crown lineToPoint:NSMakePoint(cx,      by + 92)];
        [crown lineToPoint:NSMakePoint(cx + 32, by + 36)];
        [crown lineToPoint:NSMakePoint(cx + 86, by + 76)];
        [crown lineToPoint:NSMakePoint(cx + 76, by)];
        [crown closePath];
        [[NSColor colorWithCalibratedRed:1.0 green:0.84 blue:0.18 alpha:1.0] setFill];
        [crown fill];
        [[NSColor colorWithCalibratedRed:0.78 green:0.52 blue:0.05 alpha:1.0] setStroke];
        crown.lineWidth = 6.0;
        [crown stroke];

        [NSGraphicsContext restoreGraphicsState];

        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [png writeToFile:@"/tmp/clawd_mochi_1024.png" atomically:YES];
    }
    return 0;
}
