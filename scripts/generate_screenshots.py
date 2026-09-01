#!/usr/bin/env python3
"""
Generate store screenshots mock for pebble-bpmtapper.
- States: initial (Tap to detect), 140 BPM, settings
- Platforms: basalt/diorite/flint 144x168, chalk/gabbro 180x180, emery 200x228
- No emulator needed, PIL mock matches app layout (time bar 30px, stacked BPM)

Usage:
  python3 scripts/generate_screenshots.py              # all platforms
  python3 scripts/generate_screenshots.py --platforms basalt,chalk
  python3 scripts/generate_screenshots.py --time 13:07  # custom time
"""
import argparse, os, shutil
from PIL import Image, ImageDraw, ImageFont

# 5x7 pixel digits (LECO style)
digits = {
 '0': ["01110","10001","10011","10101","11001","10001","01110"],
 '1': ["00100","01100","00100","00100","00100","00100","01110"],
 '2': ["01110","10001","00001","00010","00100","01000","11111"],
 '3': ["11111","00010","00100","00010","00001","10001","01110"],
 '4': ["00010","00110","01010","10010","11111","00010","00010"],
 '5': ["11111","10000","11110","00001","00001","10001","01110"],
 '6': ["01110","10001","10000","11110","10001","10001","01110"],
 '7': ["11111","00001","00010","00100","01000","01000","01000"],
 '8': ["01110","10001","10001","01110","10001","10001","01110"],
 '9': ["01110","10001","10001","01111","00001","00001","01110"],
}

PLATFORMS = {
    "aplite":  (144,168),
    "basalt":  (144,168),
    "diorite": (144,168),
    "flint":   (144,168),
    "chalk":   (180,180),
    "gabbro":  (260,260),
    "emery":   (200,228),
}

def load_font(size, bold=False):
    for p in [f"/usr/share/fonts/truetype/dejavu/DejaVuSans-{'Bold' if bold else ''}.ttf".replace("--","-"), "/usr/share/fonts/TTF/DejaVuSans.ttf"]:
        try:
            return ImageFont.truetype(p, size)
        except: pass
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", size)
    except:
        return ImageFont.load_default()

def draw_pixel_number(img, number_str, x0, y0, scale, fg=(255,255,255)):
    draw=ImageDraw.Draw(img)
    x=x0
    for ch in number_str:
        mat=digits[ch]
        for y,row in enumerate(mat):
            for xi,c in enumerate(row):
                if c=="1":
                    draw.rectangle([x+xi*scale, y0+y*scale, x+xi*scale+scale-1, y0+y*scale+scale-1], fill=fg)
        x+=5*scale + 1*scale
    return x

def draw_mock(w,h, state="tap", time_str="12:34"):
    img=Image.new("RGB",(w,h),(0,0,0))
    draw=ImageDraw.Draw(img)
    # top time bar 30px
    draw.rectangle([0,0,w,30], fill=(255,255,255))
    f_time=load_font(20, bold=True)
    bbox=draw.textbbox((0,0), time_str, font=f_time)
    draw.text(((w-(bbox[2]-bbox[0]))//2, (30-(bbox[3]-bbox[1]))//2 -1), time_str, font=f_time, fill=(0,0,0))

    if state=="tap":
        txt="Tap to detect"
        f=load_font(22 if w<180 else 24, bold=True)
        bb=draw.textbbox((0,0), txt, font=f)
        draw.text(((w-(bb[2]-bb[0]))//2, (h-30)//2 - (bb[3]-bb[1])//2 +15), txt, font=f, fill=(255,255,255))
    elif state=="settings":
        # mock settings MenuLayer: two rows, first highlighted
        # header bar
        # draw list
        f_title=load_font(13, bold=True)
        f_item=load_font(14, bold=False)
        f_val=load_font(13, bold=False)
        # first row highlighted (white bg black text)
        row_h=32
        y0=30+6
        # row 1 - selected
        draw.rectangle([0,y0,w,row_h+y0], fill=(255,255,255))
        draw.text((8, y0+7), "Input Mode", font=f_item, fill=(0,0,0))
        v="Touch"
        bb=draw.textbbox((0,0), v, font=f_val)
        draw.text((w-8-(bb[2]-bb[0]), y0+9), v, font=f_val, fill=(0,0,0))
        # row 2
        y0+=row_h+2
        draw.text((8, y0+7), "Metronome", font=f_item, fill=(255,255,255))
        v2="Screen+Vibe"
        bb2=draw.textbbox((0,0), v2, font=f_val)
        draw.text((w-8-(bb2[2]-bb2[0]), y0+9), v2, font=f_val, fill=(160,160,160))
        # hint bottom
        f_hint=load_font(11, bold=False)
        hint="SELECT long: settings"
        bbh=draw.textbbox((0,0), hint, font=f_hint)
        draw.text(((w-(bbh[2]-bbh[0]))//2, h-14), hint, font=f_hint, fill=(100,100,100))
    else:
        # BPM number (e.g. "140")
        num=state
        scale=5 if w==144 else 6
        w_log=len(num)*5 + (len(num)-1)*1
        pw=w_log*scale
        ph=7*scale
        block_h=ph+6+16
        base_y=30 + (h-30 - block_h)//2
        y0=base_y
        x0=(w-pw)//2
        draw_pixel_number(img, num, x0, y0, scale, (255,255,255))
        f_bpm=load_font(14 if w<180 else 16, bold=True)
        bl="BPM"
        bb2=draw.textbbox((0,0), bl, font=f_bpm)
        draw.text(((w-(bb2[2]-bb2[0]))//2, y0+ph+8), bl, font=f_bpm, fill=(255,255,255))
    return img

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--platforms", default=",".join(PLATFORMS.keys()), help="comma list e.g. basalt,chalk,emery")
    ap.add_argument("--time", default="12:34", help="time string for top bar")
    ap.add_argument("--out", default="assets/screenshots", help="output root")
    args=ap.parse_args()
    targets=[p.strip() for p in args.platforms.split(",") if p.strip() in PLATFORMS]
    if not targets:
        targets=list(PLATFORMS.keys())
    states=[("tap","01_tap.png"),("140","02_140bpm.png"),("settings","03_settings.png")]
    for name in targets:
        w,h=PLATFORMS[name]
        outdir=os.path.join(args.out, name)
        os.makedirs(outdir, exist_ok=True)
        for state, fname in states:
            img=draw_mock(w,h, state=state, time_str=args.time)
            path=os.path.join(outdir, fname)
            img.save(path,"PNG")
            print(f"saved {path} {w}x{h} {state}")
    # also copy basalt as generic for store preview
    for _, fname in states:
        src=os.path.join(args.out, "basalt", fname)
        dst=os.path.join(args.out, fname)
        if os.path.exists(src):
            shutil.copy(src, dst)
    print(f"done {len(targets)} platforms, states: initial/140/settings")

if __name__=="__main__":
    main()
