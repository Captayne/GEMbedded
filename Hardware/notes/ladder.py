#!/usr/bin/env python3
"""
ladder.py - Widerstandswerte fuer einen VGA-Farbkanal ausrechnen.

Eine gewichtete Widerstandsleiter an n GPIOs erzeugt aus einem n-Bit-Wert
eine Spannung am 75-Ohm-Eingang des Monitors.  Dieses Skript rechnet die
idealen Werte aus, zieht den Innenwiderstand des GPIO-Pads ab, rundet auf
E96 und rechnet die tatsaechliche Kennlinie nach.

Siehe Videosignal-Know-How.md, Abschnitt "Die Leiter ausrechnen".

Aufruf:  python ladder.py
"""

VG = 3.3        # GPIO-Pegel
VFS = 0.7       # Vollausschlag am Monitor (VGA-Norm: 0,7 V an 75 Ohm)
RL = 75.0       # Abschlusswiderstand im Monitor
RPAD = 40.0     # Innenwiderstand des GPIO-Pads -- ANNAHME, bitte nachmessen

E96 = [100, 102, 105, 107, 110, 113, 115, 118, 121, 124, 127, 130, 133, 137,
       140, 143, 147, 150, 154, 158, 162, 165, 169, 174, 178, 182, 187, 191,
       196, 200, 205, 210, 215, 221, 226, 232, 237, 243, 249, 255, 261, 267,
       274, 280, 287, 294, 301, 309, 316, 324, 332, 340, 348, 357, 365, 374,
       383, 392, 402, 412, 422, 432, 442, 453, 464, 475, 487, 499, 511, 523,
       536, 549, 562, 576, 590, 604, 619, 634, 649, 665, 681, 698, 715, 732,
       750, 768, 787, 806, 825, 845, 866, 887, 909, 931, 953, 976]


def e96(x):
    """Naechster E96-Wert zu x."""
    dec = 1.0
    while x / dec >= 1000:
        dec *= 10
    while x / dec < 100:
        dec /= 10
    best = None
    for v in E96:
        c = v * dec
        if best is None or abs(c - x) < abs(best - x):
            best = c
    return best


def main():
    rs = RL * (VG / VFS - 1)
    print("Geforderter Quellwiderstand der Leiter:  R_s = %.1f Ohm" % rs)
    print("(alle Zweige parallel, damit %.1f V am %.0f-Ohm-Eingang ankommen)"
          % (VFS, RL))
    print()

    for n in (3, 4, 5):
        r_lsb = rs * (2 ** n - 1)
        ideal = [r_lsb / (2 ** i) for i in range(n)]   # i=0 LSB ... n-1 MSB
        corr = [r - RPAD for r in ideal]
        chosen = [e96(r) for r in corr]

        print("%d Bit je Farbe  --  %d Farben ueber drei Kanaele"
              % (n, (2 ** n) ** 3))
        print("   Bit    ideal     minus Pad      E96")
        for i in range(n - 1, -1, -1):
            tag = "MSB" if i == n - 1 else ("LSB" if i == 0 else "b%d" % i)
            print("   %-5s %8.1f  %10.1f  %7.0f"
                  % (tag, ideal[i], corr[i], chosen[i]))

        # Kennlinie mit den gewaehlten Werten nachrechnen.  Entscheidend:
        # ein GPIO auf 0 zieht aktiv nach Masse, der Zweig bleibt also im
        # Nenner -- darum ist die Kennlinie linear.
        g = [1.0 / (chosen[i] + RPAD) for i in range(n)]
        gtot = sum(g) + 1.0 / RL
        steps = [VG * sum(g[i] for i in range(n) if code & (1 << i)) / gtot
                 for code in range(2 ** n)]
        fs = steps[-1]
        lsb = fs / (2 ** n - 1)
        dnl = max(abs((steps[k + 1] - steps[k]) / lsb - 1)
                  for k in range(2 ** n - 1))
        i_msb = (VG - fs) / (chosen[n - 1] + RPAD)
        print("   -> Vollausschlag %.3f V,  groesster Stufenfehler %.1f %% "
              "einer Stufe,  Strom MSB %.1f mA" % (fs, dnl * 100, i_msb * 1000))
        print()


if __name__ == "__main__":
    main()
