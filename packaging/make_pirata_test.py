"""Render a short known progression on the bundled grand for the CI smoke test."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pirata import piano
b = 60 / 90
prog = [[45, 52, 55, 60, 64], [41, 48, 57, 60, 64], [36, 48, 55, 60, 64], [43, 50, 55, 59, 62]]
notes = []
for i, v in enumerate(prog * 2):
    t = i * 4 * b
    for p in v:
        notes.append((t, t + 3.8 * b, p, 80))
piano.save("pirata_test_90bpm.wav", piano.render(notes))
