#!/usr/bin/env python3
"""
Mystery Box Synthesizer by Raygun -- preset library generator.

Builds the 2,500-instrument library (25 categories x 100) from hand-designed
archetypes per category plus controlled randomisation, then runs the C++
validator (tools/validate_presets.cpp) which renders every preset through the
real engine. Rejected presets are regenerated with a new seed and the loop
repeats until every preset passes. Passing presets are loudness matched by the
validator, which writes the final Resources/presets.bin.

    python3 tools/generate_presets.py --validator build/mbx_validate_presets \
        --out Resources/presets.bin

Deterministic: the same --seed always produces the same library.
"""
import argparse
import math
import os
import random
import struct
import subprocess
import sys
import tempfile

CATEGORIES = [
    "Grand & Upright Pianos", "Electric Pianos", "Organs", "Mallets & Bells",
    "Plucked Strings", "Bowed Strings", "Brass", "Woodwinds", "Analog Leads",
    "Digital Leads", "Mono Basses", "Sub & 808 Basses", "Reese & Wobble Basses",
    "Warm Pads", "Dark & Horror Pads", "Evolving Textures", "Plucks & Keys",
    "Arps & Sequencer-ready", "Choirs & Vocal Formants", "World Instruments",
    "Chiptune / 8-bit", "Synth Brass & Stabs", "FX & Risers",
    "Cinematic Hits & Drones", "Wonder Weapons",
]
COMMON, RARE, EPIC, LEGENDARY, WONDER = range(5)

# --- engine enums (mirror Source/engine/EngineParams.h) ----------------------
VA, WT, FM, PLUCK, MODAL, WAVEGUIDE, ADDITIVE = range(7)
SAW, PULSE, TRI, SINE = range(4)
L12, L24, SVF_LP, SVF_HP, SVF_BP, NOTCH, FORMANT, COMB = range(8)
# mod sources
S_LFO1, S_LFO2, S_ENVAMP, S_ENVF, S_ENVM, S_VEL, S_WHEEL, S_AT, S_KEY, S_RAND, S_SLIDE = range(1, 12)
S_MACRO1 = 12
# mod destinations
(D_PITCH, D_O1P, D_O2P, D_O3P, D_O1L, D_O2L, D_O3L, D_O1A, D_O2A, D_O3A, D_O1B, D_O2B, D_O3B,
 D_F1C, D_F1R, D_F2C, D_F2R, D_AMP, D_PAN, D_NOISE, D_DETUNE, D_L1RATE, D_L2RATE, D_SUB, D_DRIVE,
 D_ATK, D_REL, D_REVMIX, D_DLYMIX, D_CHOMIX, D_FENVAMT, D_OSCC, D_SPREAD, D_L1DEPTH, D_L2DEPTH) = range(1, 36)
# WT families (table = family*8 + variant)
WT_CLASSIC, WT_PULSE, WT_SYNC, WT_FOLD, WT_FM, WT_CHOIR, WT_ORGAN, WT_SPECTRAL = range(8)
FM_RATIOS = [0.5, 1, 1.5, 2, 3, 3.5, 4, 5, 6, 7, 8, 9, 11, 13, 14, 1.4142]
DIV_1BAR, DIV_HALF, DIV_QUARTER, DIV_8TH, DIV_8T, DIV_16TH, DIV_16T, DIV_32, DIV_4D, DIV_8D = 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
REV_HALL, REV_ROOM, REV_PLATE, REV_BUNKER, REV_ASYLUM = range(5)


def vib(semis):
    """Pitch modulation amount for a vibrato depth in semitones."""
    return semis / 24.0


def oct_amt(octaves):
    """Filter-cutoff modulation amount for a depth in octaves."""
    return octaves / 6.0


def fm_a(ratio):
    best = min(range(16), key=lambda i: abs(FM_RATIOS[i] - ratio))
    return best / 15.0


def fm_b(index):
    return math.sqrt(max(0.0, min(12.0, index)) / 12.0)


def pw_a(pw):
    return (pw - 0.05) / 0.9


def ks_decay(t60):
    return max(0.0, min(1.0, math.log(t60 / 0.25) / math.log(50)))


def modal_decay(t60):
    return max(0.0, min(1.0, math.log(t60 / 0.15) / math.log(60)))


class Preset:
    def __init__(self):
        self.p = {}
        self.mod_slots = 0

    def set(self, **kw):
        self.p.update(kw)
        return self

    def osc(self, n, mode, shape=0, a=0.5, b=0.5, c=0.0, oct=0, semi=0, fine=0.0, level=0.7,
            pan=0.0, uni=1, detune=12.0, spread=0.5):
        k = "osc%d_" % n
        self.p.update({k + "on": 1, k + "mode": mode, k + "shape": shape, k + "a": a, k + "b": b,
                       k + "c": c, k + "oct": oct, k + "semi": semi, k + "fine": fine,
                       k + "level": level, k + "pan": pan, k + "unison": uni,
                       k + "detune": detune, k + "spread": spread})
        return self

    def filt(self, n, ftype, cutoff, res=0.15, env=0.0, key=0.3, vel=0.2, drive=0.0):
        k = "f%d_" % n
        self.p.update({k + "type": ftype, k + "cutoff": cutoff, k + "res": res, k + "env": env,
                       k + "key": key})
        if n == 1:
            self.p.update({"f1_vel": vel, "f1_drive": drive})
        else:
            self.p["f2_on"] = 1
        return self

    def env(self, name, a, d, s, r, curve=0.5):
        self.p.update({name + "_a": a, name + "_d": d, name + "_s": s, name + "_r": r,
                       name + "_curve": curve})
        return self

    def lfo(self, n, shape, rate, sync=0, div=5, retrig=1, fade=0.0):
        k = "lfo%d_" % n
        self.p.update({k + "shape": shape, k + "rate": rate, k + "sync": sync, k + "div": div,
                       k + "retrig": retrig, k + "fade": fade})
        return self

    def mod(self, src, dst, amt):
        if self.mod_slots >= 8:
            return self
        self.mod_slots += 1
        k = "mod%d_" % self.mod_slots
        self.p.update({k + "src": src, k + "dst": dst, k + "amt": max(-1.0, min(1.0, amt))})
        return self

    def has_lfo_slot(self, lfo_src):
        for i in range(1, self.mod_slots + 1):
            if self.p.get("mod%d_src" % i) == lfo_src:
                return True
        return False

    def macro(self, n, value, dst1, amt1, dst2=0, amt2=0.0):
        k = "macro%d_" % n
        self.p.update({k + "value": value, k + "dst1": dst1, k + "amt1": amt1,
                       k + "dst2": dst2, k + "amt2": amt2})
        return self

    def fx(self, rev=0.15, mode=REV_HALL, size=0.5, dly=0.0, div=DIV_8D, cho=0.0):
        self.p.update({"fx_reverb_mix": rev, "fx_reverb_mode": mode, "fx_reverb_size": size,
                       "fx_delay_mix": dly, "fx_delay_div": div, "fx_chorus_mix": cho})
        return self


# ----------------------------------------------------------------------------
# Archetypes. Each function receives (rng, rarity) and returns a Preset plus a
# short descriptor list (used for tags). Values were tuned by rendering the
# results with tools/render_preset.cpp.
# ----------------------------------------------------------------------------
def U(r, a, b):
    return r.uniform(a, b)


def add_vibrato(p, r, depth=0.12, rate=5.2, fade=0.6, lfo=2):
    p.lfo(lfo, 0, U(r, rate * 0.85, rate * 1.15), fade=fade)
    p.mod(S_LFO1 if lfo == 1 else S_LFO2, D_PITCH, vib(depth))


def rarity_flourish(p, r, rarity, kind):
    """Higher rarities get richer modulation, layering and space."""
    if rarity >= RARE and not p.has_lfo_slot(S_LFO1):
        p.lfo(1, r.choice([0, 1, 5]), U(r, 0.08, 0.6), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(U(r, 0.15, 0.5)))
    if rarity >= EPIC:
        p.mod(S_RAND, D_F1C, oct_amt(U(r, 0.1, 0.3)))
        p.mod(S_AT, D_F1C, oct_amt(U(r, 0.5, 1.5)))
    if rarity >= LEGENDARY and kind not in ("bass", "perc"):
        p.mod(S_WHEEL, D_L1DEPTH, 1.0)
        p.p["fx_chorus_mix"] = max(p.p.get("fx_chorus_mix", 0.0), U(r, 0.15, 0.35))


def piano(r, rarity):
    p = Preset()
    style = r.choice(["grand", "upright", "felt", "honky", "fm", "bright"])
    bright = {"grand": U(r, .62, .8), "upright": U(r, .5, .65), "felt": U(r, .3, .45),
              "honky": U(r, .6, .75), "fm": .5, "bright": U(r, .8, .92)}[style]
    if style == "fm":
        p.osc(1, FM, a=fm_a(1), b=fm_b(U(r, 1.0, 2.2)), c=U(r, 0, .1), level=.8)
        p.osc(2, FM, a=fm_a(r.choice([3, 4])), b=fm_b(U(r, .6, 1.4)), level=.35, fine=U(r, -4, 4))
        p.env("menv", .001, U(r, .4, .9), 0, .3)
        p.mod(S_ENVM, D_O1B, U(r, .25, .45)).mod(S_ENVM, D_O2L, .4)
        p.env("amp", .002, U(r, 2.5, 4.5), 0.0, U(r, .35, .6), .8)
    else:
        decay = {"grand": U(r, 3.5, 6), "upright": U(r, 2.5, 4), "felt": U(r, 1.8, 3),
                 "honky": U(r, 2.5, 4), "bright": U(r, 3, 5)}[style]
        p.osc(1, PLUCK, a=ks_decay(decay), b=bright, c=U(r, .08, .16), level=.85)
        p.osc(2, PLUCK, a=ks_decay(decay * .9), b=bright * .95, c=U(r, .1, .2), level=.7,
              fine=(U(r, 10, 16) if style == "honky" else U(r, 1.5, 3.5)), pan=U(r, .1, .3))
        if rarity >= RARE:
            p.osc(3, PLUCK, a=ks_decay(decay * .8), b=bright, c=.12, level=.45, fine=-U(r, 1, 3), pan=-.25)
        p.mod(S_KEY, D_O1A, -.25).mod(S_KEY, D_O2A, -.25)
        p.set(noise_level=U(r, .1, .2), noise_type=1, noise_color=U(r, .5, .8), noise_decay=U(r, .1, .2))
        p.env("amp", .001, 20, 1.0, U(r, .3, .55), .8)
    p.filt(1, L12, U(r, 3500, 9000) * (0.6 if style == "felt" else 1), res=.05, key=.4, vel=U(r, .5, .8))
    p.env("fenv", .001, U(r, .8, 2.0), .4, .5)
    p.set(f1_env=U(r, .05, .15), vel_amp=U(r, .65, .85))
    p.fx(rev=U(r, .12, .28), mode=r.choice([REV_HALL, REV_ROOM, REV_PLATE]), size=U(r, .3, .7))
    return p, [style, "acoustic", "keys"], "keys"


def epiano(r, rarity):
    p = Preset()
    style = r.choice(["tine", "tine", "reed", "modal", "dyno"])
    if style in ("tine", "dyno"):
        p.osc(1, FM, a=fm_a(1), b=fm_b(U(r, .6, 1.4)), level=.85)
        p.osc(2, FM, a=fm_a(r.choice([14, 13, 11])), b=fm_b(U(r, .3, .9)), level=U(r, .25, .45))
        p.env("menv", .001, U(r, .25, .6), 0, .2)
        p.mod(S_ENVM, D_O1B, U(r, .3, .5)).mod(S_ENVM, D_O2L, .45).mod(S_VEL, D_O1B, U(r, .1, .25))
        p.env("amp", .002, U(r, 3, 5), 0.0, U(r, .4, .7), .75)
    elif style == "reed":
        p.osc(1, ADDITIVE, shape=r.randint(9, 15), a=U(r, .35, .55), b=U(r, .15, .35), level=.8)
        p.osc(2, FM, a=fm_a(1), b=fm_b(U(r, .4, .9)), level=.4)
        p.set(voice_drive=U(r, .15, .35))
        p.mod(S_VEL, D_DRIVE, U(r, .2, .4))
        p.env("amp", .002, U(r, 2, 3.5), U(r, .15, .3), U(r, .25, .45), .7)
    else:
        p.osc(1, MODAL, shape=2, a=modal_decay(U(r, 2, 4)), b=U(r, .3, .5), c=U(r, 0, .2), level=.8)
        p.osc(2, FM, a=fm_a(1), b=fm_b(U(r, .3, .7)), level=.45)
        p.env("amp", .002, 6, 1.0, U(r, .35, .6), .7)
    p.filt(1, L12, U(r, 4000, 11000), res=.08, key=.3, vel=.5)
    trem = r.choice(["pan", "amp", "none"]) if rarity < EPIC else "pan"
    if trem != "none":
        p.lfo(1, 0, U(r, 3.5, 6.5), retrig=0)
        p.mod(S_LFO1, D_PAN if trem == "pan" else D_AMP, U(r, .2, .45) if trem == "pan" else U(r, .1, .2))
    p.fx(rev=U(r, .1, .25), mode=r.choice([REV_ROOM, REV_PLATE]), cho=U(r, .15, .4))
    return p, [style, "electric", "keys"], "keys"


def organ(r, rarity):
    p = Preset()
    style = r.choice(["drawbar", "drawbar", "pipe", "combo", "gospel"])
    if style in ("drawbar", "gospel"):
        p.osc(1, WT, shape=WT_ORGAN * 8 + r.randint(0, 7), a=U(r, 0, 1), level=.8)
        if r.random() < .6:
            p.osc(2, ADDITIVE, shape=0, oct=2, level=U(r, .3, .5))  # percussion
            p.env("menv", .001, U(r, .15, .35), 0, .1)
            p.mod(S_ENVM, D_O2L, .6)
            p.set(osc2_level=.05)
        p.set(noise_level=U(r, .05, .12), noise_color=.85, noise_decay=.1)
        p.lfo(1, 0, U(r, 5.5, 7.0), retrig=0)
        p.mod(S_LFO1, D_PITCH, vib(.04))
        p.fx(rev=U(r, .1, .25), mode=REV_ROOM, cho=U(r, .35, .6))
        p.env("amp", .004, .1, 1.0, U(r, .06, .14), .3)
        if style == "gospel":
            p.set(voice_drive=U(r, .2, .45))
    elif style == "pipe":
        p.osc(1, ADDITIVE, shape=r.randint(12, 24), a=U(r, .4, .7), b=U(r, .3, .6), level=.8)
        p.osc(2, VA, shape=PULSE, a=pw_a(U(r, .3, .45)), oct=-1, level=U(r, .3, .45))
        p.osc(3, ADDITIVE, shape=r.randint(3, 6), oct=1, level=U(r, .2, .35), fine=U(r, 2, 5))
        p.env("amp", U(r, .05, .12), .2, 1.0, U(r, .5, .9), .4)
        p.fx(rev=U(r, .3, .5), mode=REV_HALL, size=U(r, .7, 1.0))
    else:
        p.osc(1, VA, shape=PULSE, a=pw_a(.5), level=.6)
        p.osc(2, VA, shape=PULSE, a=pw_a(U(r, .15, .3)), oct=1, level=.4)
        p.lfo(1, 0, U(r, 5.5, 7.0), retrig=0)
        p.mod(S_LFO1, D_PITCH, vib(.08))
        p.env("amp", .003, .1, 1.0, .08, .3)
        p.fx(rev=.15, mode=REV_ROOM)
    p.filt(1, SVF_LP, U(r, 6000, 14000), res=.05, key=.2, vel=0)
    p.set(vel_amp=U(r, 0, .2))
    return p, [style, "organ", "sustained"], "keys"


def mallet(r, rarity):
    p = Preset()
    model = r.choice([0, 1, 1, 2, 2, 3, 4, 5])
    names = {0: "bell", 1: "marimba", 2: "vibes", 3: "glass", 4: "kalimba", 5: "gong"}
    decay = {0: U(r, 3, 7), 1: U(r, .6, 1.4), 2: U(r, 2.5, 5), 3: U(r, 2, 4), 4: U(r, .8, 1.8), 5: U(r, 4, 8)}[model]
    p.osc(1, MODAL, shape=model, a=modal_decay(decay), b=U(r, .35, .8), c=U(r, 0, .25) if model in (0, 5) else U(r, 0, .08),
          level=.85, oct=(-1 if model == 5 else 0))
    if r.random() < .4 or rarity >= EPIC:
        p.osc(2, FM, a=fm_a(r.choice([3.5, 7, 1.4142, 5])), b=fm_b(U(r, .5, 2)), level=U(r, .15, .3))
        p.env("menv", .001, U(r, .3, 1.2), 0, .3)
        p.mod(S_ENVM, D_O2L, .5)
        p.set(osc2_level=.0)
    p.mod(S_VEL, D_O1B, U(r, .1, .3))
    p.env("amp", .001, 20, 1.0, U(r, .4, 1.4), .8)
    if model == 2:
        p.lfo(1, 0, U(r, 4, 6.5), retrig=0)
        p.mod(S_LFO1, D_AMP, U(r, .15, .3))
    p.filt(1, SVF_LP, U(r, 7000, 16000), res=.05, key=.2, vel=.3)
    p.fx(rev=U(r, .2, .4), mode=r.choice([REV_HALL, REV_PLATE]), size=U(r, .4, .8))
    return p, [names[model], "mallet", "percussive"], "perc"


def plucked(r, rarity):
    p = Preset()
    style = r.choice(["nylon", "steel", "harp", "koto", "banjo", "twelve", "pizz"])
    cfg = {"nylon": (U(r, 1.5, 2.6), U(r, .35, .5), U(r, .15, .25)),
           "steel": (U(r, 2, 3.5), U(r, .6, .78), U(r, .1, .2)),
           "harp": (U(r, 2.5, 4.5), U(r, .7, .85), U(r, .4, .5)),
           "koto": (U(r, 1.2, 2.2), U(r, .78, .92), U(r, .08, .14)),
           "banjo": (U(r, .5, .9), U(r, .85, .98), U(r, .05, .1)),
           "twelve": (U(r, 2, 3.5), U(r, .62, .78), U(r, .12, .2)),
           "pizz": (U(r, .35, .7), U(r, .45, .6), U(r, .2, .35))}[style]
    p.osc(1, PLUCK, a=ks_decay(cfg[0]), b=cfg[1], c=cfg[2], level=.85)
    if style == "twelve":
        p.osc(2, PLUCK, a=ks_decay(cfg[0] * .9), b=cfg[1], c=cfg[2], oct=1, fine=U(r, 2, 6), level=.5, pan=.3)
    elif rarity >= RARE:
        p.osc(2, PLUCK, a=ks_decay(cfg[0]), b=cfg[1] * .95, c=cfg[2] + .03, fine=U(r, 2, 5), level=.6, pan=U(r, -.3, .3))
    if style == "koto":
        p.env("menv", U(r, .08, .2), .3, 0, .2)
        p.mod(S_ENVM, D_PITCH, vib(-U(r, .2, .5)))
    p.mod(S_KEY, D_O1A, -.2)
    p.mod(S_VEL, D_O1B, U(r, .05, .15))
    p.env("amp", .001, 20, 1.0, U(r, .15, .4), .8)
    p.filt(1, L12, U(r, 4000, 12000), res=.05, key=.4, vel=.5)
    p.fx(rev=U(r, .12, .3), mode=r.choice([REV_ROOM, REV_HALL, REV_PLATE]))
    return p, [style, "plucked", "acoustic"], "perc"


def bowed(r, rarity):
    p = Preset()
    style = r.choice(["solo", "solo", "ensemble", "ensemble", "machine", "cello"])
    if style in ("solo", "cello"):
        p.osc(1, WAVEGUIDE, shape=1, a=U(r, .25, .55), b=U(r, .35, .7), c=U(r, .08, .3), level=.85,
              oct=(-1 if style == "cello" else 0))
        p.osc(2, VA, shape=SAW, level=U(r, .15, .25), detune=8, uni=2)
        add_vibrato(p, r, depth=U(r, .1, .22), rate=U(r, 4.8, 6.0), fade=U(r, .3, .7))
        p.env("amp", U(r, .06, .2), .5, 1.0, U(r, .3, .6), .5)
        p.filt(1, L12, U(r, 3000, 7000), res=.1, key=.3, vel=.3)
    else:
        p.osc(1, VA, shape=SAW, uni=r.randint(5, 8), detune=U(r, 9, 18), spread=U(r, .6, .95), level=.75)
        if style == "machine":
            p.osc(2, VA, shape=SAW, oct=1, uni=4, detune=U(r, 8, 14), level=.45)
            p.fx(cho=U(r, .45, .7))
        else:
            p.osc(2, WT, shape=WT_CLASSIC * 8 + r.randint(0, 3), a=U(r, .55, .75), uni=3, detune=10, level=.4)
        p.filt(1, L24, U(r, 1800, 4500), res=U(r, .05, .2), env=U(r, .05, .15), key=.4, vel=.3)
        p.env("fenv", U(r, .3, .8), 1.0, .7, 1.0)
        p.env("amp", U(r, .25, .8), 1.0, 1.0, U(r, .6, 1.5), .45)
        p.lfo(1, 0, U(r, .15, .5), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(.15))
    p.fx(rev=U(r, .25, .45), mode=REV_HALL, size=U(r, .6, .9), cho=p.p.get("fx_chorus_mix", .1))
    return p, [style, "strings", "bowed"], "pad"


def brass(r, rarity):
    p = Preset()
    style = r.choice(["trumpet", "horn", "section", "trombone", "tuba"])
    octs = {"trumpet": 0, "horn": 0, "section": 0, "trombone": -1, "tuba": -2}[style]
    p.osc(1, VA, shape=SAW, oct=octs, uni=(4 if style == "section" else 1), detune=U(r, 6, 12), level=.75)
    p.osc(2, VA, shape=SAW, oct=octs, fine=U(r, 4, 9), level=.55)
    if style == "horn":
        p.osc(3, VA, shape=TRI, oct=octs, level=.4)
    p.filt(1, L24, U(r, 350, 800), res=U(r, .05, .2), env=U(r, .4, .55), key=.5, vel=U(r, .4, .7))
    p.env("fenv", U(r, .03, .08), U(r, .3, .6), U(r, .45, .65), .3)
    p.env("amp", U(r, .02, .06), .4, .85, U(r, .15, .3), .5)
    p.mod(S_VEL, D_FENVAMT, .2)
    add_vibrato(p, r, depth=U(r, .06, .15), rate=U(r, 5, 6), fade=U(r, .4, .8))
    p.fx(rev=U(r, .15, .3), mode=r.choice([REV_HALL, REV_ROOM]))
    return p, [style, "brass", "section" if style == "section" else "solo"], "lead"


def woodwind(r, rarity):
    p = Preset()
    style = r.choice(["clarinet", "flute", "panflute", "oboe", "bassoon", "recorder"])
    if style in ("clarinet", "bassoon"):
        p.osc(1, WAVEGUIDE, shape=0, a=U(r, .3, .65), b=U(r, .35, .7), c=U(r, .1, .35), level=.85,
              oct=(-1 if style == "bassoon" else 0))
        p.filt(1, L12, U(r, 2500, 6000), res=.05, key=.3, vel=.4)
    elif style in ("flute", "panflute", "recorder"):
        p.osc(1, ADDITIVE, shape=r.randint(2, 5), a=U(r, .2, .45), b=U(r, .4, .7), level=.75)
        p.set(noise_level=U(r, .2, .38), noise_type=0, noise_color=U(r, .55, .8))
        p.filt(1, SVF_LP, U(r, 3000, 7000), res=.1, key=.5, vel=.3)
        if style == "panflute":
            p.env("menv", .001, .12, 0, .1)
            p.mod(S_ENVM, D_NOISE, .3)
    else:
        p.osc(1, VA, shape=PULSE, a=pw_a(U(r, .12, .25)), level=.7)
        p.filt(1, FORMANT, U(r, 1500, 4200), res=U(r, .3, .6), key=.1, vel=.2)
        p.filt(2, SVF_LP, 6000, res=.05)
    add_vibrato(p, r, depth=U(r, .08, .18), rate=U(r, 4.5, 5.8), fade=U(r, .4, .9))
    p.mod(S_VEL, D_AMP, .15)
    p.env("amp", U(r, .03, .09), .3, 1.0, U(r, .12, .25), .5)
    p.fx(rev=U(r, .18, .35), mode=r.choice([REV_HALL, REV_ROOM, REV_PLATE]))
    return p, [style, "woodwind", "breathy"], "lead"


def analog_lead(r, rarity):
    p = Preset()
    style = r.choice(["saw", "square", "sync", "fifths", "screamer", "soft"])
    shape1 = {"saw": SAW, "square": PULSE, "sync": SAW, "fifths": SAW, "screamer": SAW, "soft": TRI}[style]
    p.osc(1, VA, shape=shape1, a=pw_a(U(r, .35, .5)), level=.75)
    if style == "fifths":
        p.osc(2, VA, shape=SAW, semi=7, fine=U(r, 2, 6), level=.55)
    elif style == "sync":
        p.osc(2, WT, shape=WT_SYNC * 8 + r.randint(0, 3), a=U(r, .2, .5), level=.55)
        p.env("menv", .001, U(r, .2, .6), U(r, .1, .3), .3)
        p.mod(S_ENVM, D_O2A, U(r, .3, .5))
    else:
        p.osc(2, VA, shape=r.choice([SAW, PULSE]), fine=U(r, 4, 12), oct=r.choice([0, 0, -1]), level=.5)
    mono = r.random() < .7
    p.set(mono=1 if mono else 0, legato=1 if mono and r.random() < .7 else 0, glide=U(r, .02, .12) if mono else 0)
    cut = {"soft": U(r, 900, 1800), "screamer": U(r, 1200, 2500)}.get(style, U(r, 1500, 4000))
    p.filt(1, L24, cut, res=U(r, .15, .45) if style != "screamer" else U(r, .5, .7), env=U(r, .15, .35),
           key=.5, vel=.4, drive=U(r, 0, .3) if style == "screamer" else 0)
    p.env("fenv", .002, U(r, .2, .5), U(r, .3, .5), .3)
    p.env("amp", .003, .3, .9, U(r, .12, .25), .5)
    p.lfo(1, 0, U(r, 4.8, 6.2), fade=U(r, .2, .5))
    p.mod(S_LFO1, D_PITCH, vib(.06))
    p.mod(S_WHEEL, D_L1DEPTH, 1.0)
    p.fx(rev=U(r, .12, .25), dly=U(r, .12, .28), div=r.choice([DIV_8D, DIV_8TH, DIV_4D]))
    return p, [style, "lead", "mono" if mono else "poly"], "lead"


def digital_lead(r, rarity):
    p = Preset()
    style = r.choice(["wavetable", "fm", "fold", "formant", "bell"])
    if style == "wavetable":
        fam = r.choice([WT_SYNC, WT_SPECTRAL, WT_PULSE, WT_FM])
        p.osc(1, WT, shape=fam * 8 + r.randint(0, 7), a=U(r, .2, .6), uni=r.choice([1, 3, 5]), detune=U(r, 8, 18), level=.75)
        p.lfo(2, r.choice([0, 1]), U(r, .2, 1.5), retrig=0)
        p.mod(S_LFO2, D_O1A, U(r, .1, .3))
    elif style == "fm":
        p.osc(1, FM, a=fm_a(r.choice([1, 2, 3])), b=fm_b(U(r, 1.5, 4)), c=U(r, 0, .3), level=.75)
        p.env("menv", .001, U(r, .2, .6), U(r, .2, .5), .3)
        p.mod(S_ENVM, D_O1B, U(r, .2, .4))
    elif style == "fold":
        p.osc(1, WT, shape=WT_FOLD * 8 + r.randint(0, 7), a=U(r, .1, .4), level=.7)
        p.mod(S_VEL, D_O1A, U(r, .2, .4))
    elif style == "formant":
        p.osc(1, VA, shape=SAW, uni=3, detune=10, level=.7)
        p.filt(2, FORMANT, U(r, 800, 4000), res=.5, key=.0)
        p.lfo(2, 0, U(r, .3, 1.2))
        p.mod(S_LFO2, D_F2C, oct_amt(U(r, .8, 1.6)))
    else:
        p.osc(1, FM, a=fm_a(r.choice([3.5, 7, 1.4142])), b=fm_b(U(r, .8, 2)), level=.7)
        p.osc(2, VA, shape=SINE, oct=1, level=.3)
    p.osc(3, VA, shape=SINE, oct=-1, level=U(r, .15, .3))
    mono = r.random() < .5
    p.set(mono=1 if mono else 0, legato=1 if mono else 0, glide=U(r, .03, .09) if mono else 0)
    if "f1_type" not in p.p:
        p.filt(1, r.choice([L24, SVF_LP]), U(r, 2500, 9000), res=U(r, .1, .35), env=U(r, .1, .25), key=.5, vel=.4)
    p.env("fenv", .002, U(r, .2, .4), .5, .3)
    p.env("amp", .003, .3, .9, U(r, .15, .3), .5)
    p.lfo(1, 0, U(r, 5, 6.5), fade=.3)
    p.mod(S_LFO1, D_PITCH, vib(.05))
    p.mod(S_WHEEL, D_L1DEPTH, 1.0)
    p.fx(rev=U(r, .12, .28), dly=U(r, .15, .3), div=r.choice([DIV_8D, DIV_4D, DIV_16T]), mode=REV_PLATE)
    return p, [style, "digital", "lead"], "lead"


def mono_bass(r, rarity):
    p = Preset()
    style = r.choice(["saw", "square", "acid", "rubber", "pluck", "finger"])
    if style == "finger":
        p.osc(1, PLUCK, a=ks_decay(U(r, 1, 2)), b=U(r, .35, .55), c=U(r, .15, .3), oct=-1, level=.85)
        p.osc(2, VA, shape=SINE, oct=-1, level=.45)
    else:
        sh = {"saw": SAW, "square": PULSE, "acid": SAW, "rubber": PULSE, "pluck": SAW}[style]
        p.osc(1, VA, shape=sh, a=pw_a(U(r, .3, .5)), oct=-1, level=.75)
        p.osc(2, VA, shape=r.choice([SAW, PULSE, TRI]), oct=-1, fine=U(r, 3, 9), level=.45)
        p.set(sub_level=U(r, .35, .6), sub_oct=-1, sub_shape=r.choice([0, 1]))
    res = U(r, .55, .8) if style == "acid" else U(r, .1, .4)
    p.filt(1, L24, U(r, 180, 600), res=res, env=U(r, .35, .6), key=.4, vel=U(r, .3, .6),
           drive=U(r, .1, .4) if style == "acid" else U(r, 0, .2))
    p.env("fenv", .001, U(r, .12, .4), U(r, 0, .2), .15, .7)
    p.env("amp", .002, U(r, .3, .8), U(r, .6, .9) if style != "pluck" else 0.0, U(r, .05, .12), .6)
    if style == "pluck":
        p.set(amp_d=U(r, .3, .6))
    p.set(mono=1, legato=1 if r.random() < .6 else 0, glide=U(r, 0, .07) if style == "acid" else U(r, 0, .03))
    p.mod(S_VEL, D_FENVAMT, .2)
    p.fx(rev=U(r, 0, .06), mode=REV_ROOM)
    return p, [style, "bass", "mono"], "bass"


def sub808(r, rarity):
    p = Preset()
    style = r.choice(["808", "808", "sub", "distorted", "zap"])
    p.osc(1, VA, shape=SINE, oct=-1, level=.85)
    if style != "sub":
        p.osc(2, VA, shape=TRI, oct=-1, level=U(r, .2, .35))
    p.env("menv", .001, U(r, .04, .12) if style != "zap" else U(r, .15, .3), 0, .05, .8)
    drop = {"808": U(r, 1, 3), "sub": U(r, 0, .5), "distorted": U(r, 2, 5), "zap": U(r, 8, 16)}[style]
    p.mod(S_ENVM, D_PITCH, vib(drop))
    decay = U(r, .8, 2.5)
    p.env("amp", .001, decay, U(r, 0, .35) if style == "sub" else U(r, 0, .1), U(r, .15, .35), .7)
    p.set(voice_drive={"808": U(r, .05, .25), "sub": 0, "distorted": U(r, .4, .7), "zap": U(r, .1, .3)}[style])
    p.set(mono=1, glide=U(r, .02, .1) if r.random() < .5 else 0, legato=1)
    p.filt(1, SVF_LP, U(r, 1500, 5000), res=.05, key=.2, vel=.2)
    p.set(vel_amp=U(r, .3, .6))
    p.fx(rev=0.0)
    return p, [style, "sub", "808" if "808" in style else "bass"], "bass"


def reese(r, rarity):
    p = Preset()
    style = r.choice(["reese", "reese", "wobble", "wobble", "growl", "neuro"])
    if style in ("reese", "neuro"):
        p.osc(1, VA, shape=SAW, uni=r.randint(2, 4), detune=U(r, 14, 30), spread=U(r, .3, .7), oct=-1, level=.7)
        p.osc(2, VA, shape=SAW, oct=-1, fine=U(r, 8, 15), level=.5)
        p.set(sub_level=U(r, .4, .6), sub_oct=-1)
        p.filt(1, L24, U(r, 350, 1200), res=U(r, .1, .3), env=.1, key=.3)
        p.lfo(1, 5, U(r, .2, .6), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(U(r, .3, .8)))
        if style == "neuro":
            p.set(voice_drive=U(r, .3, .6))
            p.filt(2, NOTCH, U(r, 400, 1500), res=.4)
    elif style == "wobble":
        p.osc(1, VA, shape=r.choice([SAW, PULSE]), oct=-1, uni=2, detune=U(r, 8, 15), level=.7)
        p.osc(2, WT, shape=WT_FOLD * 8 + r.randint(0, 7), a=U(r, .2, .5), oct=-1, level=.45)
        p.set(sub_level=.5, sub_oct=-1)
        p.filt(1, L24, U(r, 150, 350), res=U(r, .35, .6), key=.2, drive=U(r, .2, .4))
        p.lfo(1, r.choice([0, 1]), 2.0, sync=1, div=r.choice([DIV_8TH, DIV_QUARTER, DIV_8T, DIV_16TH]))
        p.mod(S_LFO1, D_F1C, oct_amt(U(r, 2.5, 3.5)))
    else:
        p.osc(1, WT, shape=r.choice([WT_FOLD, WT_FM, WT_SYNC]) * 8 + r.randint(0, 7), a=U(r, .2, .6), oct=-1, uni=2, detune=12, level=.75)
        p.set(sub_level=.5, sub_oct=-1)
        p.lfo(1, r.choice([0, 1, 4]), 2.0, sync=1, div=r.choice([DIV_8TH, DIV_16TH, DIV_8T]))
        p.mod(S_LFO1, D_O1A, U(r, .25, .45))
        p.filt(1, L12, U(r, 800, 2500), res=.2, key=.3, drive=.3)
    p.env("amp", .003, .5, .9, U(r, .08, .2), .5)
    p.set(mono=1, legato=1, glide=U(r, 0, .06))
    p.fx(rev=U(r, 0, .08), mode=REV_ROOM)
    return p, [style, "bass", "dubstep" if style == "wobble" else "dnb"], "bass"


def warm_pad(r, rarity):
    p = Preset()
    style = r.choice(["analog", "analog", "wavetable", "glass", "string", "breath"])
    if style == "wavetable":
        p.osc(1, WT, shape=r.choice([WT_CLASSIC, WT_CHOIR, WT_SPECTRAL]) * 8 + r.randint(0, 7), a=U(r, .2, .7),
              uni=r.randint(3, 6), detune=U(r, 8, 16), spread=.85, level=.75)
        p.lfo(2, 1, U(r, .05, .2), retrig=0)
        p.mod(S_LFO2, D_O1A, U(r, .15, .3))
    elif style == "glass":
        p.osc(1, FM, a=fm_a(r.choice([2, 3, 4])), b=fm_b(U(r, .5, 1.5)), uni=3, detune=10, spread=.8, level=.6)
        p.osc(2, VA, shape=TRI, uni=4, detune=12, level=.55)
    elif style == "breath":
        p.osc(1, ADDITIVE, shape=r.randint(4, 10), a=U(r, .3, .5), b=.5, level=.6)
        p.set(noise_level=U(r, .2, .35), noise_type=1, noise_color=U(r, .4, .7))
    else:
        p.osc(1, VA, shape=SAW, uni=r.randint(4, 7), detune=U(r, 9, 18), spread=.85, level=.7)
        p.osc(2, VA, shape=r.choice([TRI, PULSE, SAW]), oct=r.choice([1, -1, 0]), uni=3, detune=10, level=.45)
    p.filt(1, L12, U(r, 1200, 3500), res=U(r, .05, .2), env=U(r, .05, .2), key=.3, vel=.2)
    p.env("fenv", U(r, .4, 1.2), 1.5, .6, 1.2)
    p.env("amp", U(r, .35, 1.2), 1.0, U(r, .75, 1.0), U(r, 1.0, 2.5), .45)
    p.lfo(1, 0, U(r, .1, .4), retrig=0)
    p.mod(S_LFO1, D_F1C, oct_amt(U(r, .15, .4)))
    p.fx(rev=U(r, .3, .5), mode=REV_HALL, size=U(r, .6, .95), cho=U(r, .2, .45))
    return p, [style, "pad", "warm"], "pad"


def dark_pad(r, rarity):
    p = Preset()
    style = r.choice(["tritone", "minor2", "choir", "rust", "abyss"])
    fam = {"choir": WT_CHOIR, "rust": WT_SPECTRAL, "abyss": WT_FOLD}.get(style, r.choice([WT_SPECTRAL, WT_CLASSIC, WT_CHOIR]))
    p.osc(1, WT, shape=fam * 8 + r.randint(0, 7), a=U(r, .2, .8), uni=r.randint(3, 6), detune=U(r, 12, 25), spread=.9, level=.7)
    interval = {"tritone": 6, "minor2": 1}.get(style, r.choice([-12, 7, 3, -5]))
    p.osc(2, VA, shape=r.choice([SAW, TRI]), semi=interval if abs(interval) < 12 else 0, oct=-1 if interval == -12 else 0,
          uni=3, detune=15, level=U(r, .3, .45))
    p.set(noise_level=U(r, .08, .2), noise_type=2, noise_color=U(r, .3, .5))
    p.filt(1, L24, U(r, 500, 1500), res=U(r, .15, .4), env=U(r, .1, .25), key=.2)
    p.env("fenv", U(r, 1, 3), 2, .6, 2)
    p.env("amp", U(r, 1, 3), 2, 1.0, U(r, 2, 4), .4)
    p.lfo(1, 5, U(r, .05, .25), retrig=0)
    p.lfo(2, 5, U(r, .1, .4), retrig=0)
    p.mod(S_LFO1, D_F1C, oct_amt(U(r, .4, .9)))
    p.mod(S_LFO2, D_PITCH, vib(U(r, .05, .2)))
    p.mod(S_LFO2, D_O1A, U(r, .1, .25))
    p.fx(rev=U(r, .4, .6), mode=r.choice([REV_ASYLUM, REV_ASYLUM, REV_BUNKER]), size=U(r, .6, 1.0))
    return p, [style, "dark", "horror", "pad"], "pad"


def texture(r, rarity):
    p = Preset()
    style = r.choice(["drift", "comb", "granular", "metal", "tidal"])
    p.osc(1, WT, shape=r.randint(0, 63), a=U(r, 0, 1), uni=r.randint(2, 5), detune=U(r, 8, 20), spread=.9, level=.65)
    p.osc(2, WT, shape=r.randint(0, 63), a=U(r, 0, 1), oct=r.choice([0, 1, -1]), level=.45)
    p.lfo(1, r.choice([0, 1, 5]), U(r, .03, .15), retrig=0)
    p.lfo(2, r.choice([0, 5]), U(r, .05, .3), retrig=0)
    p.mod(S_LFO1, D_O1A, U(r, .3, .5))
    p.mod(S_LFO2, D_O2A, U(r, .3, .5))
    if style == "comb":
        p.filt(1, COMB, U(r, 100, 800), res=U(r, .4, .7), key=1.0)
        p.filt(2, SVF_LP, U(r, 3000, 7000), res=.1)
    elif style == "metal":
        p.osc(3, MODAL, shape=r.choice([3, 5]), a=modal_decay(U(r, 3, 6)), b=.5, c=U(r, .3, .8), level=.4)
        p.filt(1, SVF_BP, U(r, 800, 3000), res=.3, key=.5)
    else:
        p.filt(1, r.choice([L12, SVF_LP, SVF_BP]), U(r, 800, 4000), res=U(r, .1, .4), key=.4)
        p.mod(S_LFO2, D_F1C, oct_amt(U(r, .5, 1.2)))
    p.set(noise_level=U(r, 0, .15), noise_type=1)
    p.env("amp", U(r, .8, 2.5), 2, 1.0, U(r, 2, 4), .4)
    p.fx(rev=U(r, .4, .6), mode=r.choice([REV_HALL, REV_ASYLUM, REV_PLATE]), size=U(r, .7, 1.0),
         dly=U(r, .1, .25), div=r.choice([DIV_4D, DIV_HALF]))
    return p, [style, "texture", "evolving", "ambient"], "pad"


def pluck_keys(r, rarity):
    p = Preset()
    style = r.choice(["saw", "pulse", "fm", "string", "glass", "house"])
    if style in ("saw", "house"):
        p.osc(1, VA, shape=SAW, uni=r.choice([1, 3, 5]), detune=U(r, 8, 15), level=.75)
        p.osc(2, VA, shape=PULSE, a=pw_a(.4), oct=r.choice([0, 1]), level=.4)
    elif style == "pulse":
        p.osc(1, VA, shape=PULSE, a=pw_a(U(r, .15, .45)), level=.75)
    elif style == "fm":
        p.osc(1, FM, a=fm_a(r.choice([1, 2, 3, 5])), b=fm_b(U(r, 1.5, 4)), level=.75)
        p.env("menv", .001, U(r, .08, .25), 0, .1)
        p.mod(S_ENVM, D_O1B, .4)
    elif style == "string":
        p.osc(1, PLUCK, a=ks_decay(U(r, .6, 1.3)), b=U(r, .75, .95), c=U(r, .1, .3), level=.85)
    else:
        p.osc(1, WT, shape=WT_SPECTRAL * 8 + r.randint(0, 7), a=U(r, .4, .9), level=.7)
    p.filt(1, L24 if style != "glass" else SVF_LP, U(r, 300, 900), res=U(r, .1, .4), env=U(r, .45, .7), key=.5, vel=.4)
    p.env("fenv", .001, U(r, .12, .35), 0.0, .2, .8)
    p.env("amp", .001, U(r, .3, .9), 0.0 if style != "house" else .25, U(r, .2, .45), .75)
    p.fx(rev=U(r, .15, .3), dly=U(r, .15, .3), div=r.choice([DIV_8D, DIV_8TH, DIV_4D]), mode=REV_PLATE)
    return p, [style, "pluck", "keys"], "perc"


def arp(r, rarity):
    p = Preset()
    style = r.choice(["blip", "acid", "random", "bell", "chord"])
    if style == "blip":
        p.osc(1, VA, shape=r.choice([PULSE, SAW]), a=pw_a(U(r, .2, .5)), level=.75)
    elif style == "acid":
        p.osc(1, VA, shape=SAW, level=.75)
        p.set(f1_res=.6)
    elif style == "random":
        p.osc(1, WT, shape=r.randint(0, 63), a=U(r, .2, .8), level=.7)
        p.lfo(2, 4, 2.0, sync=1, div=DIV_16TH, retrig=0)
        p.mod(S_LFO2, D_F1C, oct_amt(U(r, .8, 1.5)))
    elif style == "bell":
        p.osc(1, FM, a=fm_a(r.choice([3.5, 7, 3])), b=fm_b(U(r, 1, 2.5)), level=.7)
    else:
        p.osc(1, VA, shape=SAW, level=.6)
        p.osc(2, VA, shape=SAW, semi=r.choice([7, 3, 4]), level=.45)
        p.osc(3, VA, shape=SAW, semi=r.choice([10, 12]), level=.35)
    if "f1_res" in p.p:
        p.filt(1, L24, U(r, 300, 700), res=.6, env=U(r, .5, .7), key=.5, vel=.5, drive=.2)
    else:
        p.filt(1, L24, U(r, 500, 1500), res=U(r, .1, .35), env=U(r, .35, .55), key=.5, vel=.4)
    p.env("fenv", .001, U(r, .08, .2), 0.0, .1, .8)
    p.env("amp", .001, U(r, .12, .3), U(r, 0, .2), U(r, .08, .2), .8)
    p.fx(rev=U(r, .1, .2), dly=U(r, .2, .35), div=r.choice([DIV_8D, DIV_16TH, DIV_8T]), mode=REV_ROOM)
    return p, [style, "arp", "sequence"], "perc"


def choir(r, rarity):
    p = Preset()
    style = r.choice(["aah", "ooh", "chant", "robot", "angel"])
    p.osc(1, WT, shape=WT_CHOIR * 8 + r.randint(0, 7), a=U(r, 0, 1), uni=r.randint(3, 6), detune=U(r, 8, 15), spread=.8, level=.7)
    if style == "robot":
        p.osc(2, VA, shape=PULSE, a=pw_a(.3), level=.4)
        p.filt(1, FORMANT, U(r, 600, 6000), res=.6, key=.0)
        p.lfo(2, 4, 2.0, sync=1, div=DIV_8TH)
        p.mod(S_LFO2, D_F1C, oct_amt(1.5))
    else:
        p.osc(2, VA, shape=SAW, uni=3, detune=10, level=.35)
        vowel = {"aah": U(r, 60, 200), "ooh": U(r, 7000, 15000), "chant": U(r, 1500, 4000), "angel": U(r, 400, 1200)}[style]
        p.filt(1, FORMANT, vowel, res=U(r, .3, .55), key=.0)
        p.filt(2, SVF_LP, U(r, 4000, 8000), res=.05)
        p.lfo(2, 0, U(r, .1, .3), retrig=0)
        p.mod(S_LFO2, D_F1C, oct_amt(U(r, .2, .5)))
    p.set(noise_level=U(r, .05, .15), noise_color=.7)
    add_vibrato(p, r, depth=U(r, .08, .18), rate=U(r, 4.5, 5.5), fade=U(r, .5, 1.0), lfo=1)
    p.env("amp", U(r, .2, .6), .5, 1.0, U(r, .6, 1.5), .45)
    p.fx(rev=U(r, .3, .5), mode=REV_HALL, size=U(r, .6, .95), cho=U(r, .1, .3))
    return p, [style, "choir", "vocal"], "pad"


def world(r, rarity):
    p = Preset()
    style = r.choice(["sitar", "koto", "kalimba", "steeldrum", "shakuhachi", "gamelan", "dulcimer", "erhu", "oud", "bagpipe"])
    if style in ("sitar", "oud", "dulcimer", "koto"):
        bright = {"sitar": .92, "oud": .55, "dulcimer": .88, "koto": .85}[style]
        p.osc(1, PLUCK, a=ks_decay(U(r, 1.5, 3)), b=bright, c=U(r, .06, .15), level=.85)
        if style == "sitar":
            p.filt(1, COMB, U(r, 1500, 3500), res=U(r, .3, .5), key=1.0)
            p.filt(2, SVF_LP, 9000, res=.05)
            p.osc(2, PLUCK, a=ks_decay(4), b=.9, oct=1, level=.3, fine=4)
        elif style == "dulcimer":
            p.osc(2, PLUCK, a=ks_decay(2.5), b=.85, fine=U(r, 3, 6), level=.7, pan=.3)
        if style == "koto":
            p.env("menv", U(r, .1, .2), .3, 0, .2)
            p.mod(S_ENVM, D_PITCH, vib(-.3))
        p.env("amp", .001, 20, 1.0, U(r, .2, .4), .8)
    elif style == "kalimba":
        p.osc(1, MODAL, shape=4, a=modal_decay(U(r, .8, 1.8)), b=U(r, .4, .7), level=.85)
        p.env("amp", .001, 20, 1.0, .4, .8)
    elif style == "steeldrum":
        p.osc(1, FM, a=fm_a(r.choice([2, 1.5])), b=fm_b(U(r, 1, 2)), level=.7)
        p.osc(2, MODAL, shape=5, a=modal_decay(1.5), b=.6, c=.1, level=.4)
        p.env("menv", .001, .3, 0, .2)
        p.mod(S_ENVM, D_O1B, .35)
        p.env("amp", .001, U(r, .8, 1.5), 0.0, .4, .8)
    elif style == "shakuhachi":
        p.osc(1, WAVEGUIDE, shape=0, a=U(r, .25, .45), b=U(r, .3, .5), c=U(r, .5, .9), level=.8)
        p.set(noise_level=U(r, .25, .4), noise_color=.75)
        add_vibrato(p, r, depth=.2, rate=4.5, fade=.8)
        p.env("amp", U(r, .08, .2), .3, 1.0, .3, .5)
    elif style == "gamelan":
        p.osc(1, MODAL, shape=r.choice([0, 5]), a=modal_decay(U(r, 2.5, 5)), b=U(r, .5, .8), c=U(r, .4, 1.0), level=.8)
        p.osc(2, MODAL, shape=r.choice([0, 5]), a=modal_decay(3), b=.6, c=.6, fine=U(r, 8, 15), level=.6)
        p.env("amp", .001, 20, 1.0, U(r, .8, 1.5), .8)
    elif style == "erhu":
        p.osc(1, WAVEGUIDE, shape=1, a=U(r, .35, .6), b=U(r, .55, .8), c=U(r, .05, .15), level=.85)
        add_vibrato(p, r, depth=U(r, .25, .4), rate=U(r, 5.5, 6.5), fade=.3)
        p.filt(1, SVF_BP, U(r, 900, 1800), res=.15, key=.6)
        p.env("amp", U(r, .05, .12), .3, 1.0, .3, .5)
    else:  # bagpipe
        p.osc(1, VA, shape=PULSE, a=pw_a(.2), level=.6)
        p.osc(2, VA, shape=SAW, oct=-1, level=.35)
        p.filt(1, FORMANT, U(r, 2000, 5000), res=.5, key=.0)
        p.env("amp", .03, .2, 1.0, .1, .4)
    if "f1_type" not in p.p:
        p.filt(1, L12, U(r, 5000, 12000), res=.05, key=.3, vel=.4)
    p.fx(rev=U(r, .18, .35), mode=r.choice([REV_HALL, REV_ROOM, REV_PLATE]))
    return p, [style, "world", "ethnic"], "perc"


def chiptune(r, rarity):
    p = Preset()
    style = r.choice(["square", "pulse25", "pulse12", "triangle", "arp", "noise"])
    pw = {"square": .5, "pulse25": .25, "pulse12": .125}.get(style, .5)
    if style == "triangle":
        p.osc(1, VA, shape=TRI, level=.85)
    elif style == "noise":
        p.osc(1, VA, shape=PULSE, a=pw_a(.5), level=.5)
        p.set(noise_level=.6, noise_color=.9, noise_decay=U(r, .3, .5))
    else:
        p.osc(1, VA, shape=PULSE, a=pw_a(pw), level=.75)
    if style == "arp":
        p.lfo(1, 3, 2.0, sync=1, div=DIV_32)
        p.mod(S_LFO1, D_PITCH, vib(r.choice([6, 3.5, 6])))
    elif rarity >= RARE:
        add_vibrato(p, r, depth=.25, rate=6.5, fade=.25, lfo=2)
    if r.random() < .4:
        p.env("menv", .001, .05, 0, .05)
        p.mod(S_ENVM, D_PITCH, vib(r.choice([12, 5, 7])))
    p.filt(1, SVF_LP, 18000, res=.0, key=0, vel=0)
    p.env("amp", .001, U(r, .1, .35), U(r, .4, .8), U(r, .03, .08), .2)
    p.set(vel_amp=.2, mono=1 if r.random() < .5 else 0)
    p.fx(rev=U(r, 0, .1), mode=REV_ROOM, dly=U(r, 0, .2), div=DIV_8D)
    return p, [style, "chiptune", "8-bit", "retro"], "lead"


def synth_brass(r, rarity):
    p = Preset()
    style = r.choice(["poly", "stab", "jump", "fat", "brassy"])
    p.osc(1, VA, shape=SAW, uni=r.randint(3, 6), detune=U(r, 8, 16), spread=.7, level=.75)
    p.osc(2, VA, shape=r.choice([SAW, PULSE]), fine=U(r, -8, 8), oct=r.choice([0, -1]), level=.5)
    if style == "jump":
        p.osc(3, VA, shape=SAW, semi=5, level=.35)
    p.filt(1, L24, U(r, 500, 1300), res=U(r, .05, .25), env=U(r, .4, .6), key=.5, vel=.5)
    if style == "stab":
        p.env("fenv", .002, U(r, .15, .3), .1, .2)
        p.env("amp", .002, U(r, .25, .5), 0.0, U(r, .15, .3), .7)
    else:
        p.env("fenv", U(r, .02, .06), U(r, .3, .6), U(r, .35, .55), .3)
        p.env("amp", U(r, .005, .03), .4, U(r, .7, .9), U(r, .2, .4), .5)
    p.fx(rev=U(r, .15, .3), mode=REV_HALL, cho=U(r, .15, .35))
    return p, [style, "synth brass", "stab"], "lead"


def fx_riser(r, rarity):
    p = Preset()
    style = r.choice(["riser", "riser", "downlifter", "siren", "laser", "storm", "alarm"])
    if style in ("riser", "downlifter"):
        up = style == "riser"
        p.osc(1, VA, shape=SAW, uni=r.randint(4, 7), detune=U(r, 15, 30), spread=.9, level=.55)
        p.set(noise_level=U(r, .45, .7), noise_type=0, noise_color=.9)
        t = U(r, 2.0, 4.0)
        p.env("menv", t if up else .001, t if not up else 1.0, 1.0 if up else 0.0, .5, .3)
        p.mod(S_ENVM, D_PITCH, vib(U(r, 7, 12) * (1 if up else -1)))
        p.mod(S_ENVM, D_F1C, oct_amt(U(r, 3, 5) * (1 if up else -1)))
        p.filt(1, SVF_BP if r.random() < .5 else L12, U(r, 300, 800) if up else U(r, 2000, 6000), res=U(r, .2, .5))
        p.env("amp", U(r, .5, 1.5) if up else .01, 1.0, 1.0, U(r, 1, 2.5), .4)
    elif style in ("siren", "alarm"):
        p.osc(1, VA, shape=r.choice([SAW, PULSE, TRI]), level=.6)
        p.lfo(1, 1 if style == "siren" else 3, U(r, .3, 1.0) if style == "siren" else U(r, 2, 4))
        p.mod(S_LFO1, D_PITCH, vib(U(r, 4, 7)))
        p.filt(1, L12, U(r, 2000, 5000), res=.2)
        p.env("amp", .05, .5, 1.0, .5, .5)
    elif style == "laser":
        p.osc(1, FM, a=fm_a(r.choice([1, 2, 1.4142])), b=fm_b(U(r, 2, 5)), level=.6)
        p.env("menv", .001, U(r, .15, .4), 0, .2, .8)
        p.mod(S_ENVM, D_PITCH, vib(U(r, 18, 24)))
        p.mod(S_ENVM, D_O1B, .4)
        p.filt(1, SVF_LP, 12000, res=.1)
        p.env("amp", .001, U(r, .3, .6), 0.0, .3, .7)
    else:
        p.set(noise_level=.8, noise_type=r.choice([1, 2]), noise_color=.7)
        p.osc(1, VA, shape=SINE, oct=-2, level=.4)
        p.lfo(1, 5, U(r, .1, .3), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(2.0))
        p.filt(1, SVF_BP, U(r, 400, 900), res=.3)
        p.env("amp", U(r, .8, 1.5), 1, 1.0, 2, .4)
    p.fx(rev=U(r, .35, .55), mode=r.choice([REV_HALL, REV_ASYLUM]), size=.85, dly=U(r, .1, .25), div=DIV_4D)
    return p, [style, "fx", "transition"], "fx"


def cinematic(r, rarity):
    p = Preset()
    style = r.choice(["hit", "hit", "boom", "drone", "drone", "braam", "tension"])
    if style in ("hit", "boom"):
        p.osc(1, VA, shape=SINE, oct=-2, level=.85)
        p.osc(2, MODAL, shape=5, a=modal_decay(U(r, 2, 4)), b=U(r, .6, .9), c=U(r, .3, .8), oct=-1, level=.5)
        p.set(noise_level=U(r, .4, .6), noise_type=1, noise_color=U(r, .4, .7), noise_decay=U(r, .45, .6))
        p.env("menv", .001, U(r, .1, .25), 0, .2, .8)
        p.mod(S_ENVM, D_PITCH, vib(U(r, 7, 14)))
        p.set(voice_drive=U(r, .2, .45))
        p.env("amp", .001, U(r, 1.5, 3.5), 0.0, U(r, 1, 2), .8)
        p.filt(1, SVF_LP, U(r, 2000, 6000), res=.05)
    elif style == "braam":
        p.osc(1, VA, shape=SAW, oct=-1, uni=6, detune=U(r, 15, 25), spread=.9, level=.7)
        p.osc(2, VA, shape=SAW, oct=-2, uni=3, detune=10, level=.5)
        p.filt(1, L24, U(r, 250, 500), res=.2, env=.45)
        p.env("fenv", U(r, .3, .8), 1.5, .4, 1.5)
        p.set(voice_drive=U(r, .3, .5))
        p.env("amp", U(r, .05, .2), 2.0, .8, U(r, 1.5, 3), .5)
    else:
        p.osc(1, WT, shape=r.choice([WT_SPECTRAL, WT_CHOIR, WT_CLASSIC]) * 8 + r.randint(0, 7), a=U(r, .2, .8),
              oct=-1, uni=4, detune=U(r, 10, 20), spread=.9, level=.6)
        p.osc(2, VA, shape=SAW, oct=-2, level=.45)
        p.set(sub_level=.35, sub_oct=-1)
        p.filt(1, L24, U(r, 300, 1200), res=U(r, .2, .45), key=.2)
        p.lfo(1, 5, U(r, .03, .12), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(U(r, .8, 1.6)))
        if style == "tension":
            p.lfo(2, 0, U(r, 4, 7))
            p.mod(S_LFO2, D_AMP, U(r, .3, .5))
        p.env("amp", U(r, 1.5, 3.5), 2, 1.0, U(r, 2.5, 4), .4)
    p.fx(rev=U(r, .35, .55), mode=r.choice([REV_HALL, REV_ASYLUM, REV_BUNKER]), size=U(r, .7, 1.0))
    return p, [style, "cinematic", "drone" if "drone" in style else "impact"], "fx"


def wonder(r, rarity):
    p = Preset()
    style = r.choice(["raygun", "thundergun", "freeze", "plasma", "portal", "tesla", "singularity", "phaser"])
    if style == "raygun":
        p.osc(1, FM, a=fm_a(r.choice([1.4142, 2, 3.5])), b=fm_b(U(r, 2, 5)), c=U(r, .2, .5), level=.65)
        p.osc(2, VA, shape=SINE, oct=1, level=.35)
        p.env("menv", .001, U(r, .15, .35), 0, .2, .8)
        p.mod(S_ENVM, D_PITCH, vib(U(r, 12, 24)))
        p.lfo(1, 0, U(r, 8, 18))
        p.mod(S_LFO1, D_O1B, U(r, .2, .4))
        p.filt(1, SVF_LP, 14000, res=.1)
        p.env("amp", .001, U(r, .4, .9), U(r, .2, .5), .4, .6)
    elif style == "thundergun":
        p.osc(1, VA, shape=SAW, oct=-1, uni=5, detune=30, level=.6)
        p.set(noise_level=.5, noise_type=2)
        p.filt(1, COMB, U(r, 60, 200), res=.6, key=1.0)
        p.lfo(1, 4, U(r, 6, 14))
        p.mod(S_LFO1, D_F1C, oct_amt(.8))
        p.set(voice_drive=.5)
        p.env("amp", .005, 1.0, .6, .8, .5)
    elif style == "freeze":
        p.osc(1, MODAL, shape=3, a=modal_decay(U(r, 3, 6)), b=.9, c=U(r, .5, 1.0), level=.6)
        p.osc(2, WT, shape=WT_SPECTRAL * 8 + r.randint(4, 7), a=.8, uni=5, detune=25, level=.4)
        p.lfo(1, 4, U(r, 10, 20))
        p.mod(S_LFO1, D_O2A, .3)
        p.filt(1, SVF_HP, U(r, 300, 800), res=.2)
        p.env("amp", .01, 2.0, .5, 1.5, .5)
    elif style == "plasma":
        p.osc(1, WT, shape=WT_FOLD * 8 + r.randint(0, 7), a=.3, uni=4, detune=20, level=.6)
        p.lfo(1, 1, U(r, 3, 9))
        p.mod(S_LFO1, D_O1A, .45)
        p.filt(1, L24, U(r, 800, 2500), res=.6, drive=.4)
        p.lfo(2, 0, U(r, .2, .6))
        p.mod(S_LFO2, D_F1C, oct_amt(2))
        p.env("amp", .01, .5, .9, .5, .5)
    elif style == "portal":
        p.osc(1, VA, shape=SAW, uni=7, detune=U(r, 30, 60), spread=1.0, level=.5)
        p.osc(2, FM, a=fm_a(1.4142), b=fm_b(3), level=.35)
        p.filt(1, FORMANT, U(r, 200, 8000), res=.6, key=0)
        p.lfo(1, 0, U(r, .2, .7), retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(2.5))
        p.env("amp", U(r, .3, .8), 1, 1.0, 2.0, .4)
    elif style == "tesla":
        p.osc(1, VA, shape=PULSE, a=pw_a(.1), level=.55)
        p.set(noise_level=.4, noise_color=1.0)
        p.lfo(1, 4, U(r, 15, 35))
        p.mod(S_LFO1, D_PITCH, vib(U(r, 1, 3)))
        p.mod(S_LFO1, D_AMP, .4)
        p.filt(1, SVF_BP, U(r, 1500, 4000), res=.4, key=.5)
        p.env("amp", .002, .4, .7, .3, .6)
    elif style == "singularity":
        p.osc(1, VA, shape=SINE, oct=-2, level=.7)
        p.osc(2, FM, a=fm_a(.5), b=fm_b(U(r, 4, 8)), oct=-1, level=.4)
        p.env("menv", U(r, 1, 2), 1, 1.0, 1, .3)
        p.mod(S_ENVM, D_O2B, -.3)
        p.lfo(1, 0, U(r, .1, .3))
        p.mod(S_LFO1, D_PITCH, vib(.5))
        p.filt(1, L24, 1500, res=.3)
        p.env("amp", U(r, .5, 1.2), 1, 1.0, 2.0, .4)
    else:
        p.osc(1, VA, shape=SAW, uni=3, detune=15, level=.6)
        p.filt(1, NOTCH, U(r, 400, 2000), res=.6)
        p.lfo(1, 0, U(r, 2, 6))
        p.mod(S_LFO1, D_F1C, oct_amt(2.5))
        p.env("amp", .005, .5, .9, .4, .5)
    p.fx(rev=U(r, .3, .5), mode=r.choice([REV_PLATE, REV_ASYLUM, REV_HALL]), size=.8, dly=U(r, .15, .35), div=DIV_8D)
    return p, [style, "wonder", "sci-fi", "experimental"], "fx"


ARCHETYPES = [piano, epiano, organ, mallet, plucked, bowed, brass, woodwind, analog_lead, digital_lead,
              mono_bass, sub808, reese, warm_pad, dark_pad, texture, pluck_keys, arp, choir, world,
              chiptune, synth_brass, fx_riser, cinematic, wonder]

# ----------------------------------------------------------------------------
# Names: original, bunker-themed + musical vocabulary
# ----------------------------------------------------------------------------
THEME_ADJ = ["Bunker", "Barricade", "Chalkboard", "Rusted", "Midnight", "Concrete", "Lantern", "Sandbag",
             "Searchlight", "Outpost", "Quarantine", "Generator", "Ironclad", "Fallout", "Ration", "Static",
             "Survivor", "Ember", "Ashfall", "Moonlit", "Gravel", "Tin Can", "Last Stand", "Power-Out",
             "Cellar", "Rooftop", "Boarded", "Flicker", "Gaslight", "Radio", "Signal", "Hollow", "Scrapyard",
             "Foxhole", "Daybreak", "Nightwatch", "Sirens", "Watchtower", "Morse", "Fuse Box", "Overgrown",
             "Abandoned", "Old Town", "Undertow", "Grim", "Lucky", "Wildcard", "Odd Crate", "Spare Part",
             "Patched", "Riveted", "Dusty", "Smoky", "Neon", "Cracked", "Sealed", "Haunted", "Distant"]
CAT_NOUNS = {
    0: ["Grand", "Upright", "Parlor Piano", "Baby Grand", "Saloon Keys", "Felt Piano", "Hammer Keys", "Concert Grand", "Chapel Piano", "Tack Piano", "Spinet", "Ballroom Grand"],
    1: ["Rhodes", "Tines", "Suitcase Keys", "Reed Piano", "Stage EP", "Dyno Keys", "Lounge EP", "Bark Keys", "Velvet Tines", "Soul EP"],
    2: ["Organ", "Drawbars", "Chapel Pipes", "Combo Organ", "Gospel B", "Pump Organ", "Cathedral", "Rink Organ", "Tonewheel", "Harmonium"],
    3: ["Bell", "Marimba", "Vibes", "Glass Harp", "Kalimba", "Gong", "Chimes", "Glock", "Celesta", "Music Box", "Tubular Bell", "Mallets"],
    4: ["Guitar", "Nylon", "Harp", "Koto", "Banjo", "Twelve-String", "Pizzicato", "Lute", "Mandolin", "Zither", "Strummer"],
    5: ["Strings", "Violin", "Cello", "Ensemble", "String Machine", "Viola", "Quartet", "Bowed Wire", "Chamber"],
    6: ["Trumpet", "Horn", "Brass", "Trombone", "Tuba", "Bugle", "Cornet", "Fanfare", "Horn Section"],
    7: ["Clarinet", "Flute", "Pan Flute", "Oboe", "Bassoon", "Recorder", "Reed", "Whistle", "Fife"],
    8: ["Lead", "Saw Lead", "Square Lead", "Sync Lead", "Screamer", "Solo", "Mono Lead", "Wire Lead"],
    9: ["Wave Lead", "FM Lead", "Fold Lead", "Vox Lead", "Bit Lead", "Glass Lead", "Circuit"],
    10: ["Bass", "Mono Bass", "Acid Line", "Rubber Bass", "Finger Bass", "Pluck Bass", "Low End", "Bassline"],
    11: ["808", "Sub", "Boom", "Kick Bass", "Low Rumble", "Sub Drop", "Thump"],
    12: ["Reese", "Wobble", "Growl", "Neuro Bass", "Snarl", "Grinder", "Wub"],
    13: ["Pad", "Warm Pad", "Blanket", "Haze", "Glow", "Hearth", "Cloud", "Sunrise Pad"],
    14: ["Dread", "Shadows", "Crypt Pad", "Nightmare", "Abyss", "Whispers", "Lurker", "Hollow Pad"],
    15: ["Texture", "Drift", "Tide", "Field", "Murmur", "Fog", "Shimmer", "Machinery"],
    16: ["Pluck", "Keys", "Blip", "Pluck Keys", "Mallet Pluck", "Glass Pluck", "Snap"],
    17: ["Arp", "Sequence", "Pulse Arp", "Ticker", "Stepper", "Loop", "Pattern"],
    18: ["Choir", "Voices", "Chant", "Vox", "Aahs", "Oohs", "Hymn", "Chorus"],
    19: ["Sitar", "Koto", "Kalimba", "Steel Drum", "Shakuhachi", "Gamelan", "Dulcimer", "Erhu", "Oud", "Bagpipe"],
    20: ["Chip", "8-Bit", "Square Wave", "Pulse", "Arcade", "Cartridge", "Bleep", "Pixel"],
    21: ["Brass Stab", "Poly Brass", "Jump Brass", "Stab", "Synth Horns", "Brass Pad"],
    22: ["Riser", "Downlifter", "Siren", "Laser", "Storm", "Alarm", "Sweep", "Whoosh"],
    23: ["Hit", "Boom", "Drone", "Braam", "Tension", "Impact", "Rumble", "Dread Drone"],
    24: ["Ray", "Thunder Cannon", "Freeze Ray", "Plasma Coil", "Portal", "Tesla Arc", "Singularity", "Phase Blaster",
         "Wonder Coil", "Zap Ray", "Ion Beam", "Gravity Gun"],
}
SPECIAL_NAMES = {
    0: ["Bunker Grand", "Chalkboard Upright"], 1: ["Bunker Rhodes"], 3: ["Chalkboard Bell"], 10: ["Barricade Bass"],
    23: ["Round 99 Drone"], 24: ["The Last Ray"],
}
ROMAN = ["II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"]


def make_name(r, cat, used):
    for n in SPECIAL_NAMES.get(cat, []):
        if n not in used:
            used.add(n)
            return n
    for _ in range(200):
        form = r.random()
        noun = r.choice(CAT_NOUNS[cat])
        if form < 0.75:
            name = "%s %s" % (r.choice(THEME_ADJ), noun)
        elif form < 0.88:
            name = "%s Round %d" % (noun, r.randint(2, 99))
        else:
            name = "%s %s" % (noun, r.choice(["of the Damned", "at Dawn", "in the Dark", "Unboxed", "Revived", "Overdrive", "Mk %d" % r.randint(2, 9)]))
        if name not in used and len(name) <= 30:
            used.add(name)
            return name
    base = "%s %s" % (r.choice(THEME_ADJ), r.choice(CAT_NOUNS[cat]))
    for rn in ROMAN:
        if base + " " + rn not in used:
            used.add(base + " " + rn)
            return base + " " + rn
    raise RuntimeError("name space exhausted")


def assign_macros(p, kind):
    """The 8 macros: Tone, Character, Movement, Space, Attack, Release, Grit, Width."""
    mode1 = p.p.get("osc1_mode", VA)
    p.macro(1, 0.5, D_F1C, 0.6, D_FENVAMT, 0.25)
    char_dst = {WT: D_O1A, FM: D_O1B, PLUCK: D_O1B, MODAL: D_O1B, WAVEGUIDE: D_O1A, ADDITIVE: D_O1A}.get(mode1, D_O2L)
    p.macro(2, 0.5, D_F1R, 0.5, char_dst, 0.5)
    if not p.has_lfo_slot(S_LFO1):
        if "lfo1_rate" not in p.p:
            p.lfo(1, 0, 0.35 if kind == "pad" else 4.5, retrig=0)
        p.mod(S_LFO1, D_F1C, oct_amt(0.12) if kind != "lead" else oct_amt(0.08))
    p.macro(3, 0.0, D_L1DEPTH, 1.0, D_L1RATE, 0.3)
    p.macro(4, 0.0, D_REVMIX, 0.55, D_DLYMIX, 0.35)
    p.macro(5, 0.25, D_ATK, 1.0)
    p.macro(6, 0.35, D_REL, 1.0)
    p.macro(7, 0.0, D_DRIVE, 0.7, D_NOISE, 0.15)
    uni = max(p.p.get("osc1_unison", 1), p.p.get("osc2_unison", 1))
    p.macro(8, 0.5, D_SPREAD, 0.8, D_DETUNE if uni > 1 else D_CHOMIX, 0.8 if uni > 1 else 0.5)


def rarity_table(cat, r):
    if cat == 24:
        pool = [WONDER] * 50 + [LEGENDARY] * 20 + [EPIC] * 30
    else:
        pool = [COMMON] * 62 + [RARE] * 26 + [EPIC] * 9 + [LEGENDARY] * 3
    r.shuffle(pool)
    return pool


def build_preset(pid, cat, rarity, seed, used_names):
    r = random.Random(seed)
    p, tags, kind = ARCHETYPES[cat](r, rarity)
    rarity_flourish(p, r, rarity, kind)
    if rarity == WONDER:
        p.mod(S_RAND, D_PITCH, vib(r.choice([0, 0, 12, 7, -12])))
    assign_macros(p, kind)
    name = make_name(r, cat, used_names)
    return {"id": pid, "category": cat, "rarity": rarity, "name": name, "tags": ",".join(tags), "params": p.p}


# ----------------------------------------------------------------------------
def load_param_table(validator):
    out = subprocess.run([validator, "--dump-params"], check=True, capture_output=True, text=True).stdout
    table = []
    for line in out.strip().splitlines():
        pid, mn, mx, df = line.split()
        table.append((pid, float(mn), float(mx), float(df)))
    return table


def write_bin(path, presets, table):
    ids = [t[0] for t in table]
    defaults = {t[0]: t[3] for t in table}
    lim = {t[0]: (t[1], t[2]) for t in table}
    with open(path, "wb") as f:
        f.write(b"MBXP" + struct.pack("<I", 1))
        f.write(struct.pack("<H", len(ids)))
        for i in ids:
            b = i.encode()
            f.write(struct.pack("<B", len(b)) + b)
        f.write(struct.pack("<H", len(CATEGORIES)))
        for c in CATEGORIES:
            b = c.encode()
            f.write(struct.pack("<B", len(b)) + b)
        f.write(struct.pack("<I", len(presets)))
        for pr in presets:
            unknown = set(pr["params"]) - set(ids)
            if unknown:
                raise KeyError("unknown params %s in %s" % (unknown, pr["name"]))
            name = pr["name"].encode()
            tags = pr["tags"].encode()[:255]
            f.write(struct.pack("<HBBB", pr["id"], pr["category"], pr["rarity"], len(name)) + name)
            f.write(struct.pack("<B", len(tags)) + tags)
            vals = []
            for i in ids:
                v = float(pr["params"].get(i, defaults[i]))
                lo, hi = lim[i]
                vals.append(min(hi, max(lo, v)))
            f.write(struct.pack("<%df" % len(vals), *vals))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--validator", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=0x5A7E)
    ap.add_argument("--report", default=None)
    ap.add_argument("--max-rounds", type=int, default=25)
    args = ap.parse_args()

    table = load_param_table(args.validator)
    master = random.Random(args.seed)
    used = set()
    presets, seeds = [], []
    pid = 1
    for cat in range(25):
        rarities = rarity_table(cat, master)
        for i in range(100):
            seed = master.randrange(1 << 30)
            seeds.append(seed)
            presets.append(build_preset(pid, cat, rarities[i], seed, used))
            pid += 1

    tmp = tempfile.mkdtemp(prefix="mbx_presets_")
    raw, report = os.path.join(tmp, "raw.bin"), os.path.join(tmp, "report.txt")
    attempt = {}
    for rnd in range(args.max_rounds):
        write_bin(raw, presets, table)
        subprocess.run([args.validator, raw, args.out, report], check=False)
        failed = []
        with open(report) as f:
            for line in f:
                parts = line.split(" ", 2)
                if parts[1] == "FAIL":
                    failed.append((int(parts[0]), parts[2].strip()))
        print("round %d: %d failed" % (rnd + 1, len(failed)))
        if not failed:
            break
        for fid, why in failed:
            idx = fid - 1
            pr = presets[idx]
            print("  regenerating #%04d %-28s (%s)" % (fid, pr["name"], why))
            used.discard(pr["name"])
            attempt[fid] = attempt.get(fid, 0) + 1
            presets[idx] = build_preset(fid, pr["category"], pr["rarity"], seeds[idx] * 31 + attempt[fid] * 7919, used)
    else:
        print("ERROR: presets still failing after %d rounds" % args.max_rounds)
        sys.exit(1)

    if args.report:
        with open(report) as src, open(args.report, "w") as dst:
            dst.write(src.read())
    print("wrote %s (%d presets)" % (args.out, len(presets)))


if __name__ == "__main__":
    main()
