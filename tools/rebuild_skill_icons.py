#!/usr/bin/env python3
"""
Download skill icons from mu.lv and rebuild Skill.OZJ spritesheet.

Skill.OZJ layout: 25 icons per row, each 20x28px in original (256x256 sheet).
We build at 2x (40x56px icons, 512x512 sheet), then save as OZJ.
Icon index = skill ID. Empty slots filled with original.
"""
import subprocess, io, os, struct, sys, tempfile
from PIL import Image

DATA_DIR = os.path.dirname(os.path.abspath(__file__)) + "/../client/Data"
SKILL_OZJ = DATA_DIR + "/Interface/Skill.OZJ"
ESRGAN = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/realesrgan-ncnn-vulkan"
MODEL_DIR = os.path.dirname(os.path.abspath(__file__)) + "/realesrgan/models"

# Icon grid: 25 per row, each cell 20x28 at 1x
COLS = 25
CELL_W_1X = 20
CELL_H_1X = 28

# mu.lv icon URLs mapped to skill IDs
# From CLAUDE.md skill table + Main 5.2 skill IDs
SKILL_ICONS = {
    # DW skills
    "bfmi.webp":  1,   # Poison
    "1wmf.webp":  2,   # Meteorite
    "ebmw.webp":  3,   # Lightning
    "68mb.webp":  4,   # Fire Ball
    "y1m8.webp":  5,   # Flame
    "3km1.webp":  6,   # Teleport
    "fzmc.webp":  7,   # Ice
    "bpmz.webp":  8,   # Twister
    "1xmp.webp":  9,   # Evil Spirit
    "edmx.webp": 10,   # Hellfire
    "65md.webp": 11,   # Power Wave
    "yhm5.webp": 12,   # Aqua Beam
    "32mh.webp": 13,   # Cometfall (Blast)
    "4im2.webp": 14,   # Inferno
    "rfmi.webp": 15,   # Teleport Ally
    "awmf.webp": 16,   # Soul Barrier
    "w8mw.webp": 17,   # Energy Ball

    # DK skills
    "81mb.webp": 18,   # Defense (shield)
    "kkm8.webp": 19,   # Falling Slash
    "7em1.webp": 20,   # Lunge
    "dpmc.webp": 21,   # Uppercut
    "hxmz.webp": 22,   # Cyclone
    "idmp.webp": 23,   # Slash

    # Elf skills
    "k2m5.webp": 26,   # Heal
    "7imh.webp": 27,   # Greater Defense
    "jfm2.webp": 28,   # Greater Damage
    "vbmf.webp": 30,   # Summon Goblin
    "o8mw.webp": 31,   # Summon Stone Golem
    "l1mb.webp": 32,   # Summon Assassin
    "mkm8.webp": 33,   # Summon Elite Yeti
    "0em1.webp": 34,   # Summon Dark Knight
    "jpmc.webp": 35,   # Summon Bali
    "zdmz.webp": 36,   # Summon Soldier

    # Shared / class-change skills
    "bfmh.webp": 41,   # Twisting Slash
    "1wm2.webp": 42,   # Rageful Blow (Anger Strike)
    "ebmi.webp": 43,   # Death Stab

    # MG skills
    "hwmh.webp": 44,   # Fire Slash (MG)
    "ibm2.webp": 45,   # Power Slash (MG)

    # Elf ranged
    "w5mx.webp": 24,   # Triple Shot
    "yhmx.webp": 46,   # Penetration

    # DW 2nd class
    "5hmx.webp": 38,   # Decay
    "22md.webp": 39,   # Ice Storm
    "fim5.webp": 40,   # Nova

    # Elf 2nd class
    "65mp.webp": 51,   # Ice Arrow
    "x5mc.webp": 47,   # Infinity Arrow
    "b7mx.webp": 48,   # Recovery (SD restore)
    "16md.webp": 49,   # Multi-Shot

    # DK extras
    "r7m1.webp": 37,   # Swell Life
    "2kmz.webp": 50,   # Strike of Destruction

    # MG extras
    "ejm5.webp": 52,   # Flame Strike (MG)
    "6ymh.webp": 53,   # Gigantic Storm (MG)

    # Castle Siege / special
    "68mf.webp": 25,   # Crescent Moon Slash
    "3kmb.webp": 29,   # Starfall (Elf CS)
    "ejm1.webp": 57,   # Plasma Storm (Fenrir)
    "1xmc.webp": 56,   # Fire Breath (Dinorant)

    # Buff skills
    "k2mz.webp": 54,   # Blessing of Experience
    "9wmh.webp": 55,   # Swell Mana
    "femp.webp": 58,   # Expansion of Wizardry

    # MG CS skills
    "w8mi.webp": 59,   # Spiral Slash
    "mkmw.webp": 60,   # Mana Rays
}


def download_icon(filename):
    """Download a skill icon from mu.lv, return PIL Image or None."""
    url = f"https://mu.lv/img/skils/{filename}"
    tmp = tempfile.mktemp(suffix=".webp")
    rc = subprocess.run(
        ["curl", "-s", "-o", tmp, "-A", "Mozilla/5.0", url],
        capture_output=True
    ).returncode
    if rc != 0:
        return None
    try:
        img = Image.open(tmp).convert("RGB")
        return img
    except:
        return None
    finally:
        try: os.unlink(tmp)
        except: pass


def upscale_image(img, scale=2):
    """Upscale a PIL image using ESRGAN."""
    tmp_in = tempfile.mktemp(suffix=".png")
    tmp_out = tempfile.mktemp(suffix=".png")
    img.save(tmp_in)
    rc = subprocess.run(
        [ESRGAN, "-i", tmp_in, "-o", tmp_out, "-s", str(scale),
         "-n", "realesrgan-x4plus-anime", "-m", MODEL_DIR],
        capture_output=True
    ).returncode
    result = None
    if rc == 0:
        try:
            result = Image.open(tmp_out).convert("RGB")
        except:
            pass
    for f in [tmp_in, tmp_out]:
        try: os.unlink(f)
        except: pass
    return result


def main():
    # Load original Skill.OZJ as fallback
    data = open(SKILL_OZJ, "rb").read()
    hdr_off = 0
    orig = None
    for off in [0, 24]:
        try:
            orig = Image.open(io.BytesIO(data[off:]))
            orig.load()
            hdr_off = off
            break
        except:
            orig = None

    if orig is None:
        print("ERROR: Cannot read original Skill.OZJ")
        sys.exit(1)

    print(f"Original Skill.OZJ: {orig.size}, header offset={hdr_off}")

    # Target: 2x sheet (512x512 with 40x56 cells)
    SCALE = 2
    cell_w = CELL_W_1X * SCALE  # 40
    cell_h = CELL_H_1X * SCALE  # 56
    sheet_w = COLS * cell_w     # 1000
    # Calculate rows needed (original is 256/28 ≈ 9 rows)
    orig_rows = (orig.size[1] + CELL_H_1X - 1) // CELL_H_1X
    sheet_h = orig_rows * cell_h

    print(f"Target sheet: {sheet_w}x{sheet_h} ({COLS} cols x {orig_rows} rows, {cell_w}x{cell_h} cells)")

    # Start with upscaled original as base
    print("Upscaling original sheet as base...")
    base = orig.resize((sheet_w, sheet_h), Image.LANCZOS)

    # Download and place each icon
    total = len(SKILL_ICONS)
    ok = 0
    fail = 0
    for i, (filename, skill_id) in enumerate(sorted(SKILL_ICONS.items(), key=lambda x: x[1])):
        row = skill_id // COLS
        col = skill_id % COLS
        x = col * cell_w
        y = row * cell_h

        if y + cell_h > sheet_h:
            print(f"  [{i+1}/{total}] skill {skill_id} ({filename}) - OUT OF BOUNDS, skip")
            fail += 1
            continue

        print(f"  [{i+1}/{total}] skill {skill_id:3d} ({filename}) -> row {row} col {col} ...", end=" ", flush=True)

        icon = download_icon(filename)
        if icon is None:
            print("DOWNLOAD FAIL")
            fail += 1
            continue

        # Resize to cell size
        icon_resized = icon.resize((cell_w, cell_h), Image.LANCZOS)
        base.paste(icon_resized, (x, y))
        print("OK")
        ok += 1

    print(f"\nPlaced {ok}/{total} icons ({fail} failed)")

    # Save as OZJ (JPEG with original header)
    backup = SKILL_OZJ + ".bak"
    if not os.path.exists(backup):
        import shutil
        shutil.copy2(SKILL_OZJ, backup)
        print(f"Backup: {backup}")

    jpg_buf = io.BytesIO()
    base.save(jpg_buf, "JPEG", quality=98)
    hdr = data[:hdr_off]
    with open(SKILL_OZJ, "wb") as f:
        f.write(hdr + jpg_buf.getvalue())

    print(f"Saved: {SKILL_OZJ} ({sheet_w}x{sheet_h})")
    print("Done!")


if __name__ == "__main__":
    main()
