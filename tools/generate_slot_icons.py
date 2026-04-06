#!/usr/bin/env python3
"""
Generate detailed equipment slot background silhouette PNGs for MU Online remaster.

Each icon is drawn at 4x resolution with internal padding and downscaled with
LANCZOS for clean anti-aliasing. Semi-transparent silhouettes on transparent backgrounds.
"""

from PIL import Image, ImageDraw, ImageFilter
import os, math

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "client", "Data", "Interface")

# Color palette — dark steel with cool blue-grey tones
C_FILL   = (155, 165, 180)   # Main body fill
C_EDGE   = (185, 195, 210)   # Edge highlights
C_DETAIL = (130, 140, 155)   # Inner detail lines
C_ACCENT = (120, 140, 175)   # Blue accent
C_DARK   = (100, 110, 125)   # Shadow areas

# Alpha levels
A_FILL   = 65
A_EDGE   = 100
A_DETAIL = 45
A_ACCENT = 55
A_DARK   = 40

# Padding: pixels of margin at final resolution (before 4x scaling)
PAD_LARGE  = 5   # For 46-wide slots
PAD_TALL   = 4   # For 46x66 tall slots (less horizontal, more vertical)
PAD_SMALL  = 3   # For 28x28 small slots


def _c(color, alpha):
    return color + (alpha,)


def make_weapon_r(w=46, h=66):
    """Right-hand weapon: detailed ornate sword with padding."""
    S = 4
    P = PAD_TALL * S  # padding in hi-res coords
    iw, ih = w*S - 2*P, h*S - 2*P  # inner drawable area
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2

    # All coords offset by P
    top = P
    bot = h*S - P

    # Blade body
    blade_w = max(4, iw // 8)
    d.polygon([
        (cx, top),                          # tip
        (cx - blade_w, top + 5*S),          # left edge start
        (cx - blade_w, bot - 18*S),         # left edge end
        (cx - blade_w - 2*S, bot - 16*S),   # guard flare L
        (cx + blade_w + 2*S, bot - 16*S),   # guard flare R
        (cx + blade_w, bot - 18*S),         # right edge end
        (cx + blade_w, top + 5*S),          # right edge start
    ], fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Blade center fuller
    fw = max(1, blade_w // 3)
    d.rectangle([cx - fw, top + 7*S, cx + fw, bot - 20*S], fill=_c(C_DARK, A_DARK))

    # Edge highlights
    d.line([(cx - blade_w + S, top + 6*S), (cx - blade_w + S, bot - 18*S)],
           fill=_c(C_EDGE, A_ACCENT), width=S)
    d.line([(cx + blade_w - S, top + 6*S), (cx + blade_w - S, bot - 18*S)],
           fill=_c(C_EDGE, A_ACCENT), width=S)

    # Cross-guard
    guard_w = iw // 3
    gy = bot - 17*S
    d.rounded_rectangle([cx - guard_w, gy, cx + guard_w, gy + 3*S],
                         radius=S, fill=_c(C_EDGE, A_FILL+20), outline=_c(C_EDGE, A_EDGE), width=S)

    # Grip
    grip_w = max(2*S, blade_w - S)
    grip_top = gy + 3*S
    grip_bot = bot - 5*S
    d.rectangle([cx - grip_w, grip_top, cx + grip_w, grip_bot],
                fill=_c(C_DETAIL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)
    for y in range(grip_top + 2*S, grip_bot, 3*S):
        d.line([(cx - grip_w, y), (cx + grip_w, y + 2*S)],
               fill=_c(C_DARK, A_DETAIL), width=S)

    # Pommel
    d.ellipse([cx - 3*S, bot - 6*S, cx + 3*S, bot],
              fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_weapon_l(w=46, h=66):
    """Left-hand: ornate kite shield with padding."""
    S = 4
    P = PAD_TALL * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = w*S//2, h*S//2

    # Shield outline with padding
    rx = (w*S - 2*P) * 0.48
    ry = (h*S - 2*P) * 0.46
    pts = []
    n = 60
    for i in range(n):
        t = i / n
        angle = t * 2 * math.pi - math.pi/2
        x = cx + rx * math.cos(angle)
        y = cy - 2*S + ry * math.sin(angle)
        if math.sin(angle) > 0.2:
            factor = 1.0 - (math.sin(angle) - 0.2) * 0.8
            x = cx + (x - cx) * max(factor, 0.05)
        pts.append((x, y))
    d.polygon(pts, fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Inner border
    irx, iry = rx * 0.78, ry * 0.78
    ipts = []
    for i in range(n):
        t = i / n
        angle = t * 2 * math.pi - math.pi/2
        x = cx + irx * math.cos(angle)
        y = cy - 2*S + iry * math.sin(angle)
        if math.sin(angle) > 0.2:
            factor = 1.0 - (math.sin(angle) - 0.2) * 0.8
            x = cx + (x - cx) * max(factor, 0.05)
        ipts.append((x, y))
    d.polygon(ipts, fill=_c(C_DARK, A_DARK), outline=_c(C_DETAIL, A_DETAIL), width=S)

    # Central cross
    cr = int(ry * 0.55)
    d.line([(cx, cy - cr), (cx, cy + cr + 4*S)], fill=_c(C_EDGE, A_DETAIL+10), width=2*S)
    d.line([(cx - int(rx*0.5), cy), (cx + int(rx*0.5), cy)], fill=_c(C_EDGE, A_DETAIL+10), width=2*S)

    # Central emblem
    em = int(min(rx, ry) * 0.2)
    d.polygon([(cx, cy-em), (cx+em, cy), (cx, cy+em), (cx-em, cy)],
              fill=_c(C_ACCENT, A_ACCENT), outline=_c(C_EDGE, A_EDGE), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_cap(w=46, h=46):
    """Helmet with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2
    L, R, T, B = P, w*S - P, P, h*S - P

    # Main dome
    d.ellipse([L+2*S, T, R-2*S, B-4*S],
              fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Brow ridge
    brow_y = (T + B) // 2
    d.arc([L, brow_y - 4*S, R, B],
          200, 340, fill=_c(C_EDGE, A_EDGE), width=2*S)

    # Visor slits
    slit_y = brow_y + 2*S
    for dy in range(0, 6*S, 3*S):
        d.line([(L+6*S, slit_y + dy), (R-6*S, slit_y + dy)],
               fill=_c(C_DARK, A_FILL), width=S)

    # Nose guard
    d.line([(cx, brow_y - 2*S), (cx, B - 6*S)],
           fill=_c(C_EDGE, A_EDGE), width=2*S)

    # Crown crest
    d.polygon([(cx, T-S), (cx - 3*S, T + 4*S), (cx + 3*S, T + 4*S)],
              fill=_c(C_EDGE, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)

    # Decorative arcs
    d.arc([L+4*S, T+2*S, R-4*S, brow_y],
          210, 330, fill=_c(C_DETAIL, A_DETAIL), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_upper(w=46, h=66):
    """Armor/chest plate with padding."""
    S = 4
    P = PAD_TALL * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2
    L, R, T, B = P, w*S - P, P, h*S - P

    # Main torso
    torso = [
        (L+4*S, T+6*S),          # left shoulder
        (cx - 2*S, T+2*S),       # left neck
        (cx + 2*S, T+2*S),       # right neck
        (R-4*S, T+6*S),          # right shoulder
        (R-2*S, T+22*S),         # right armpit
        (R-5*S, B-2*S),          # right hip
        (cx + 2*S, B),           # bottom right
        (cx - 2*S, B),           # bottom left
        (L+5*S, B-2*S),          # left hip
        (L+2*S, T+22*S),         # left armpit
    ]
    d.polygon(torso, fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Shoulder pauldrons
    d.ellipse([L, T+3*S, L+12*S, T+15*S],
              fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)
    d.ellipse([R-12*S, T+3*S, R, T+15*S],
              fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)

    # Collar
    d.arc([cx-7*S, T, cx+7*S, T+10*S], 200, 340,
          fill=_c(C_EDGE, A_EDGE), width=2*S)

    # Center seam
    d.line([(cx, T+10*S), (cx, B-4*S)], fill=_c(C_DETAIL, A_DETAIL), width=S)

    # Belt
    belt_y = B - 14*S
    d.line([(L+6*S, belt_y), (R-6*S, belt_y)],
           fill=_c(C_EDGE, A_DETAIL+10), width=2*S)
    d.rounded_rectangle([cx-3*S, belt_y-2*S, cx+3*S, belt_y+2*S],
                         radius=S, fill=_c(C_ACCENT, A_ACCENT), outline=_c(C_EDGE, A_EDGE), width=S)

    # Chest segments
    for y_off in [14*S, 22*S, 30*S]:
        d.arc([L+5*S, T+y_off, R-5*S, T+y_off + 10*S], 10, 170,
              fill=_c(C_DETAIL, A_DETAIL), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_lower(w=46, h=46):
    """Pants/leg armor with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2
    L, R, T, B = P, w*S - P, P, h*S - P
    gap = 3*S

    # Waistband
    d.rounded_rectangle([L+2*S, T, R-2*S, T+6*S],
                         radius=2*S, fill=_c(C_EDGE, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)
    # Belt buckle
    d.rounded_rectangle([cx-3*S, T+S, cx+3*S, T+5*S],
                         radius=S, fill=_c(C_ACCENT, A_ACCENT), outline=_c(C_EDGE, A_EDGE), width=S)

    # Left leg
    d.polygon([
        (L+2*S, T+6*S), (cx - gap, T+6*S),
        (cx - gap - S, B), (L, B),
    ], fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Right leg
    d.polygon([
        (cx + gap, T+6*S), (R-2*S, T+6*S),
        (R, B), (cx + gap + S, B),
    ], fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Knee guards
    knee_y = (T + B) // 2 - 3*S
    kw = 9*S
    # Left
    lkx = L + 3*S
    d.ellipse([lkx, knee_y, lkx + kw, knee_y + kw],
              fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)
    # Right
    rkx = R - 3*S - kw
    d.ellipse([rkx, knee_y, rkx + kw, knee_y + kw],
              fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)

    # Segment lines
    for y in [T + 10*S, B - 8*S]:
        d.line([(L+3*S, y), (cx - gap - S, y)], fill=_c(C_DETAIL, A_DETAIL), width=S)
        d.line([(cx + gap + S, y), (R-3*S, y)], fill=_c(C_DETAIL, A_DETAIL), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_gloves(w=46, h=46):
    """Gauntlet with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2
    L, R, T, B = P, w*S - P, P, h*S - P

    # Cuff
    d.rounded_rectangle([L+3*S, B-10*S, R-3*S, B],
                         radius=3*S, fill=_c(C_FILL, A_FILL+10), outline=_c(C_EDGE, A_EDGE), width=S)

    # Palm
    d.ellipse([L+2*S, T+8*S, R-2*S, B-6*S],
              fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Fingers (4)
    iw = R - L - 6*S
    fw = iw // 5  # finger width
    for i in range(4):
        fx = L + 3*S + i * (fw + S)
        # Two segments per finger
        d.rounded_rectangle([fx, T, fx + fw, T + 5*S],
                             radius=S, fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)
        d.rounded_rectangle([fx, T + 5*S, fx + fw, T + 10*S],
                             radius=S, fill=_c(C_FILL, A_FILL-5), outline=_c(C_EDGE, A_EDGE-10), width=S)

    # Thumb
    d.ellipse([L, T+12*S, L+7*S, T+22*S],
              fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Knuckle guard
    d.rounded_rectangle([L+3*S, T+9*S, R-3*S, T+13*S],
                         radius=2*S, fill=_c(C_EDGE, A_DETAIL), outline=_c(C_EDGE, A_EDGE-20), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_boots(w=46, h=46):
    """Armored boot with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    L, R, T, B = P, w*S - P, P, h*S - P

    # Shaft
    d.rounded_rectangle([L+5*S, T, R-5*S, T+20*S],
                         radius=3*S, fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)
    # Top cuff
    d.rounded_rectangle([L+3*S, T, R-3*S, T+5*S],
                         radius=2*S, fill=_c(C_EDGE, A_FILL+15), outline=_c(C_EDGE, A_EDGE), width=S)

    # Ankle
    d.polygon([
        (L+3*S, T+20*S), (R-3*S, T+20*S),
        (R, T+26*S), (L-S, T+26*S),
    ], fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Foot
    d.rounded_rectangle([L-S, T+26*S, R+S, B],
                         radius=3*S, fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Sole
    d.rounded_rectangle([L, B-4*S, R, B],
                         radius=2*S, fill=_c(C_DARK, A_FILL), outline=_c(C_EDGE, A_EDGE-20), width=S)

    # Buckle straps
    for y in [T+10*S, T+16*S]:
        cx_boot = (L + R) // 2
        d.line([(L+5*S, y), (R-5*S, y)], fill=_c(C_DETAIL, A_DETAIL+10), width=S)
        d.rounded_rectangle([cx_boot-3*S, y-S, cx_boot+3*S, y+S],
                             radius=S, fill=_c(C_ACCENT, A_ACCENT))

    # Shin guard
    d.rounded_rectangle([L+8*S, T+5*S, R-8*S, T+18*S],
                         radius=2*S, fill=_c(C_FILL, A_DETAIL), outline=_c(C_DETAIL, A_DETAIL), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_wing(w=61, h=46):
    """Wings with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = w*S//2, h*S//2 + 2*S
    L, R, T, B = P, w*S - P, P, h*S - P

    # Available spread radius
    max_spread = (R - L) // 2 - 2*S

    def draw_wing_side(d, cx, cy, direction=1):
        for i, (angle, length_pct, fw) in enumerate([
            (25, 0.95, 3), (15, 1.0, 3), (5, 0.95, 2),
            (-5, 0.88, 2), (-15, 0.75, 2), (-25, 0.60, 1),
        ]):
            rad = math.radians(angle * direction)
            length = int(max_spread * length_pct)
            x1 = cx + direction * 2*S
            y1 = cy - 2*S
            x2 = x1 + int(direction * length * math.cos(rad))
            y2 = y1 - int(length * math.sin(rad))
            # Clamp to bounds
            x2 = max(L, min(R, x2))
            y2 = max(T, min(B, y2))

            perp_x = -math.sin(rad) * fw * S
            perp_y = math.cos(rad) * fw * S
            pts = [
                (x1, y1),
                (x2 + perp_x*0.3, y2 + perp_y*0.3),
                (x2, y2),
                (x2 - perp_x*0.3, y2 - perp_y*0.3),
            ]
            alpha = A_FILL - i * 3
            d.polygon(pts, fill=_c(C_FILL, alpha), outline=_c(C_EDGE, alpha + 20))
            d.line([(x1, y1), (x2, y2)], fill=_c(C_DETAIL, A_DETAIL), width=S)

    draw_wing_side(d, cx, cy, direction=-1)
    draw_wing_side(d, cx, cy, direction=1)

    # Center body
    d.ellipse([cx-3*S, cy-3*S, cx+3*S, cy+7*S],
              fill=_c(C_FILL, A_FILL+15), outline=_c(C_EDGE, A_EDGE), width=S)

    # Coverts
    for direction in [-1, 1]:
        for i, angle in enumerate(range(30, -10, -12)):
            rad = math.radians(angle * direction)
            length = int(max_spread * 0.35) - i * 2*S
            x1 = cx + direction * 2*S
            y1 = cy + 2*S
            x2 = x1 + int(direction * length * math.cos(rad))
            y2 = y1 - int(length * math.sin(rad))
            d.line([(x1, y1), (x2, y2)], fill=_c(C_DETAIL, A_DETAIL - 5), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_fairy(w=46, h=46):
    """Pet/fairy with padding."""
    S = 4
    P = PAD_LARGE * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = w*S//2, h*S//2
    L, R, T, B = P, w*S - P, P, h*S - P

    # Body
    d.ellipse([cx-5*S, cy+1*S, cx+5*S, cy+11*S],
              fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Head
    d.ellipse([cx-6*S, cy-10*S, cx+6*S, cy+3*S],
              fill=_c(C_FILL, A_FILL), outline=_c(C_EDGE, A_EDGE), width=S)

    # Eyes
    d.ellipse([cx-3*S, cy-6*S, cx-1*S, cy-3*S], fill=_c(C_ACCENT, A_ACCENT))
    d.ellipse([cx+1*S, cy-6*S, cx+3*S, cy-3*S], fill=_c(C_ACCENT, A_ACCENT))

    # Wings (left) — stay within bounds
    wing_l = [
        (cx-5*S, cy-2*S),
        (max(L+S, L+2*S), cy-12*S),
        (max(L, L+S), cy-2*S),
        (max(L+2*S, L+3*S), cy+4*S),
        (cx-5*S, cy+3*S),
    ]
    d.polygon(wing_l, fill=_c(C_FILL, A_FILL-15), outline=_c(C_EDGE, A_EDGE-20), width=S)
    d.line([(cx-5*S, cy), (L+3*S, cy-8*S)], fill=_c(C_DETAIL, A_DETAIL), width=S)

    # Wings (right)
    wing_r = [
        (cx+5*S, cy-2*S),
        (min(R-S, R-2*S), cy-12*S),
        (min(R, R-S), cy-2*S),
        (min(R-2*S, R-3*S), cy+4*S),
        (cx+5*S, cy+3*S),
    ]
    d.polygon(wing_r, fill=_c(C_FILL, A_FILL-15), outline=_c(C_EDGE, A_EDGE-20), width=S)
    d.line([(cx+5*S, cy), (R-3*S, cy-8*S)], fill=_c(C_DETAIL, A_DETAIL), width=S)

    # Halo
    d.ellipse([cx-7*S, cy-16*S, cx+7*S, cy-11*S],
              fill=None, outline=_c(C_ACCENT, A_ACCENT+10), width=2*S)

    # Sparkles — keep within bounds
    for sx, sy in [(L+4*S, cy-4*S), (R-4*S, cy-6*S), (cx, B-4*S)]:
        sz = S
        d.line([(sx-2*sz, sy), (sx+2*sz, sy)], fill=_c(C_EDGE, A_DETAIL), width=sz)
        d.line([(sx, sy-2*sz), (sx, sy+2*sz)], fill=_c(C_EDGE, A_DETAIL), width=sz)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_necklace(w=28, h=28):
    """Necklace with padding."""
    S = 4
    P = PAD_SMALL * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx = w*S//2
    L, R, T, B = P, w*S - P, P, h*S - P

    # Chain arc
    d.arc([L+S, T, R-S, B-4*S], 195, 345,
          fill=_c(C_EDGE, A_EDGE), width=2*S)

    # Chain links
    arc_rx = (R - L - 2*S) / 2
    arc_ry = (B - T - 4*S) / 2
    arc_cx = cx
    arc_cy = (T + B - 4*S) // 2
    for angle in range(200, 345, 10):
        rad = math.radians(angle)
        x = arc_cx + int(arc_rx * math.cos(rad))
        y = arc_cy + int(arc_ry * math.sin(rad))
        d.ellipse([x-S, y-S, x+S, y+S], fill=_c(C_DETAIL, A_DETAIL+20))

    # Chains to pendant
    d.line([(L+3*S, (T+B)//2 - 2*S), (cx-3*S, B-7*S)], fill=_c(C_EDGE, A_EDGE-10), width=2*S)
    d.line([(R-3*S, (T+B)//2 - 2*S), (cx+3*S, B-7*S)], fill=_c(C_EDGE, A_EDGE-10), width=2*S)

    # Pendant gem
    gem = [
        (cx, B-10*S),
        (cx - 4*S, B-5*S),
        (cx, B-S),
        (cx + 4*S, B-5*S),
    ]
    d.polygon(gem, fill=_c(C_ACCENT, A_ACCENT+10), outline=_c(C_EDGE, A_EDGE), width=S)
    d.line([(cx, B-10*S), (cx, B-S)], fill=_c(C_DETAIL, A_DARK), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def make_ring(w=28, h=28):
    """Ring with padding."""
    S = 4
    P = PAD_SMALL * S
    img = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = w*S//2, h*S//2 + 2*S
    L, R, T, B = P, w*S - P, P, h*S - P

    # Ring band
    band = Image.new("RGBA", (w*S, h*S), (0,0,0,0))
    bd = ImageDraw.Draw(band)
    bd.ellipse([L+S, T+5*S, R-S, B], fill=_c(C_FILL, A_FILL))
    bd.ellipse([L+5*S, T+8*S, R-5*S, B-3*S], fill=(0,0,0,0))
    img = Image.alpha_composite(img, band)
    d = ImageDraw.Draw(img)

    # Outlines
    d.ellipse([L+S, T+5*S, R-S, B], fill=None, outline=_c(C_EDGE, A_EDGE), width=S)
    d.ellipse([L+5*S, T+8*S, R-5*S, B-3*S], fill=None, outline=_c(C_DETAIL, A_DETAIL), width=S)

    # Gem
    d.ellipse([cx-4*S, T, cx+4*S, T+8*S],
              fill=_c(C_ACCENT, A_ACCENT+15), outline=_c(C_EDGE, A_EDGE), width=S)
    d.ellipse([cx-2*S, T+S, cx+S, T+5*S], fill=_c(C_EDGE, A_DARK))

    # Prongs
    d.line([(cx-3*S, T+7*S), (cx-4*S, T+4*S)], fill=_c(C_EDGE, A_EDGE-20), width=S)
    d.line([(cx+3*S, T+7*S), (cx+4*S, T+4*S)], fill=_c(C_EDGE, A_EDGE-20), width=S)

    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img.resize((w, h), Image.LANCZOS)


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    slots = [
        ("newui_item_weapon_r.png", make_weapon_r, 46, 66),
        ("newui_item_weapon_l.png", make_weapon_l, 46, 66),
        ("newui_item_cap.png",      make_cap,      46, 46),
        ("newui_item_upper.png",    make_upper,    46, 66),
        ("newui_item_lower.png",    make_lower,    46, 46),
        ("newui_item_gloves.png",   make_gloves,   46, 46),
        ("newui_item_boots.png",    make_boots,    46, 46),
        ("newui_item_wing.png",     make_wing,     61, 46),
        ("newui_item_fairy.png",    make_fairy,    46, 46),
        ("newui_item_necklace.png", make_necklace, 28, 28),
        ("newui_item_ring.png",     make_ring,     28, 28),
    ]

    for filename, gen_func, w, h in slots:
        img = gen_func(w, h)
        path = os.path.join(OUTPUT_DIR, filename)
        img.save(path, "PNG")
        print(f"  ✓ {filename} ({w}x{h})")

    print(f"\nAll {len(slots)} slot icons generated in:\n  {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
