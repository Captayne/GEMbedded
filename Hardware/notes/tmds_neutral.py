#!/usr/bin/env python3
"""
tmds_neutral.py - welche TMDS-Symbole darf man ohne Zustand wiederholen?

Rechnet den Standard-TMDS-Kodierer (DVI 1.0) fuer alle 256 Bytewerte durch
und beantwortet drei Fragen:

  1. Wo landet der Bilanzzaehler, wenn derselbe Wert zweimal ab cnt=0
     kodiert wird?  (Antwort: meist bei +-2, nicht bei 0.)
  2. Welche Werte haben ein Symbol, das *selbst* ausgeglichen ist
     (5 Einsen, 5 Nullen)?  Nur diese darf eine PIO-State-Machine beliebig
     oft wiederholen, ohne den Gleichanteil zu verschieben.
  3. Wie weit liegt ein gewuenschter Farbwert vom naechsten neutralen weg,
     bei 5 und 6 Bit je Kanal?

Siehe Videosignal-Know-How.md, Abschnitt 4.5.

Aufruf:  python tmds_neutral.py
"""


def stage1(d):
    """Uebergangsminimierung: liefert (q_m[0..7], q_m[8])."""
    n1 = bin(d).count("1")
    xnor = n1 > 4 or (n1 == 4 and (d & 1) == 0)
    q = d & 1
    prev = q
    for i in range(1, 8):
        b = (d >> i) & 1
        nb = (1 - (prev ^ b)) if xnor else (prev ^ b)
        q |= nb << i
        prev = nb
    return q, (0 if xnor else 1)


def encode(d, cnt):
    """Ein Byte kodieren; liefert (10-Bit-Symbol, neuer Zaehlerstand)."""
    q, q8 = stage1(d)
    ones = bin(q).count("1")
    zeros = 8 - ones
    if cnt == 0 or ones == zeros:
        if q8:
            sym = q | (1 << 8)
            cnt += ones - zeros
        else:
            sym = (~q & 0xFF) | (1 << 9)
            cnt += zeros - ones
    elif (cnt > 0 and ones > zeros) or (cnt < 0 and zeros > ones):
        sym = (~q & 0xFF) | (q8 << 8) | (1 << 9)
        cnt += 2 * q8 + zeros - ones
    else:
        sym = q | (q8 << 8)
        cnt += -2 * (1 - q8) + ones - zeros
    return sym, cnt


def disparity(sym):
    """Einsen minus Nullen im 10-Bit-Symbol."""
    ones = bin(sym & 0x3FF).count("1")
    return ones - (10 - ones)


def forms(d):
    """Normal- und invertierte Form eines Wertes."""
    q, q8 = stage1(d)
    normal = q | (q8 << 8)
    inverted = (~q & 0xFF) | (q8 << 8) | (1 << 9)
    return normal, inverted


def main():
    # 1. Derselbe Wert zweimal ab cnt=0
    back_to_zero = 0
    for d in range(256):
        _, c1 = encode(d, 0)
        _, c2 = encode(d, c1)
        if c2 == 0:
            back_to_zero += 1
    print("Gleicher Wert zweimal ab cnt=0:")
    print("   endet wieder bei 0:  %3d von 256" % back_to_zero)
    print("   endet bei +-2:       %3d von 256   (driftet bei jeder Wiederholung)"
          % (256 - back_to_zero))
    print()

    # 2. Selbst neutrale Symbole
    neutral = [d for d in range(256)
               if any(disparity(f) == 0 for f in forms(d))]
    gap = max(b - a for a, b in zip(neutral, neutral[1:]))
    print("Werte mit selbst neutralem Symbol (5 Einsen / 5 Nullen):")
    print("   %d von 256,  Bereich %d .. %d,  groesste Luecke %d"
          % (len(neutral), neutral[0], neutral[-1], gap))
    for name, d in (("Schwarz 0x00", 0x00), ("Weiss   0xFF", 0xFF)):
        n, i = forms(d)
        print("   %s: Formen %+d / %+d  -> %s"
              % (name, disparity(n), disparity(i),
                 "neutral" if d in neutral else "nicht neutral"))
    print()

    # Normal + invertiert: immer +-2
    pair_sums = {disparity(n) + disparity(i) for n, i in map(forms, range(256))}
    print("Normal- plus invertierte Form, ueber alle Werte:", sorted(pair_sums))
    print()

    # 3. Abstand zum naechsten neutralen Wert
    print("Groesster Abstand eines Farbwerts zum naechsten neutralen:")
    for bits in (5, 6):
        levels = (1 << bits) - 1
        worst = max(min(abs(lvl * 255 // levels - n) for n in neutral)
                    for lvl in range(levels + 1))
        print("   %d Bit je Kanal: %d  (%.1f %% des Vollausschlags)"
              % (bits, worst, worst * 100.0 / 255))
    print()
    print("Neutrale Werte:", neutral)


if __name__ == "__main__":
    main()
