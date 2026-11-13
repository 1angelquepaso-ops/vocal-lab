"""Hand-written styles (everything the Groove MIDI Dataset doesn't cover) plus the
groove-layer recipes for every style.

Patterns are 16 steps per bar ("x" = accent, "o" = soft, "." = rest) or 12 steps
for triplet styles. Fill "families" name the procedural fill builders in generate.py.
`gmd` = which Groove MIDI Dataset styles lend their real fills / drummer feel.
"""

STYLES = {
    # ---------------------------------------------------------------- hip-hop family
    "trap": dict(name="Trap", swing=0.0, human=0.15, roll="1/32T",
                 perc="......x.......x.", shaker="..x...x...x...x.", ghost=".......o......o.",
                 fills=["trap_roll", "snare_roll", "drop"], gmd=["hiphop"]),
    "drill": dict(name="Drill (UK / NY)", swing=0.0, human=0.15, roll="1/16T",
                  perc="...x......x.....", shaker="x..x..x.x..x..x.", ghost="..o.......o..o..",
                  fills=["trap_roll", "drop", "snare_roll"], gmd=["hiphop"]),
    "boombap": dict(name="Boom bap", swing=0.18, human=0.5, roll="1/16",
                    perc="..x.......x..x..", shaker="x.x.x.x.x.x.x.x.", ghost="......o..o....o.",
                    fills=["snare_roll", "toms_down", "chop"], gmd=["hiphop", "funk"]),
    "hiphop": dict(name="Hip-hop (live)", swing=0.1, human=0.7, roll="1/16",
                   perc="..x.......x.....", shaker="xoxoxoxoxoxoxoxo", ghost=".o....o..o....o.",
                   fills=["gmd", "snare_roll", "toms_down"], gmd=["hiphop"]),
    "rnb": dict(name="R&B / neo-soul", swing=0.25, human=0.6, roll="1/32",
                perc="...x......x..x..", shaker="xoxoxoxoxoxoxoxo", ghost="..o..o....o..o..",
                fills=["gmd", "snare_roll", "chop"], gmd=["soul", "funk"]),
    # ---------------------------------------------------------------- latin / caribbean
    "reggaeton": dict(name="Reggaeton / dembow", swing=0.0, human=0.2, roll="1/16",
                      perc="...x..x....x..x.", shaker="x.x.x.x.x.x.x.x.", ghost="..o.......o.....",
                      fills=["snare_roll", "timbal", "drop"], gmd=["latin/reggaeton", "latin"]),
    "cumbia": dict(name="Cumbia", swing=0.05, human=0.5, roll="1/16",
                   perc="x.xxx.xxx.xxx.xx", shaker="x.ox.ox.x.ox.ox.", ghost="......o.......o.",
                   fills=["timbal", "snare_roll"], gmd=["latin"]),
    "banda": dict(name="Banda / tambora", swing=0.0, human=0.5, roll="1/16",
                  perc="x...x.x.x...x.x.", shaker="..x...x...x...x.", ghost="......o.......o.",
                  fills=["tarola", "timbal", "snare_roll"], gmd=["latin", "rock/shuffle"]),
    "corridos": dict(name="Corridos tumbados", swing=0.08, human=0.35, roll="1/32T",
                     perc="..x..x....x..x..", shaker="x.ox.ox.x.ox.ox.", ghost="..o.......o.....",
                     fills=["tarola", "trap_roll", "drop"], gmd=["latin", "hiphop"]),
    "salsa": dict(name="Salsa (Tito's)", swing=0.0, human=0.6, roll="1/16",
                  perc="x..x..x...x.x...", shaker="xoxxxoxxxoxxxoxx", ghost=".o...o...o...o..",
                  fills=["timbal", "abanico"], gmd=["afrocuban", "latin"]),
    "bachata": dict(name="Bachata", swing=0.0, human=0.5, roll="1/16",
                    perc="xoxxxoxxxoxxxoxx", shaker="x.x.x.x.x.x.x.x.", ghost="",
                    fills=["timbal", "snare_roll"], gmd=["latin"]),
    "merengue": dict(name="Merengue", swing=0.0, human=0.5, roll="1/16",
                     perc="x.xxx.xxx.xxx.xx", shaker="xxxxxxxxxxxxxxxx", ghost="",
                     fills=["gmd", "timbal"], gmd=["latin/merengue", "latin/dominican-merengue"]),
    "samba": dict(name="Samba / bossa", swing=0.1, human=0.6, roll="1/16",
                  perc="x.x..x.x..x.x.x.", shaker="xoxoxoxoxoxoxoxo", ghost="",
                  fills=["gmd", "timbal"], gmd=["latin/samba", "latin/brazilian-samba", "latin/brazilian-bossa"]),
    "afrocuban": dict(name="Afro-Cuban (bembe / rumba)", swing=0.0, human=0.7, roll="1/16T",
                      perc="x.x.xx.x.x.x", shaker="xoxoxoxoxoxo", ghost="", steps=12,
                      fills=["gmd", "timbal"], gmd=["afrocuban"]),
    "dancehall": dict(name="Dancehall", swing=0.05, human=0.3, roll="1/16",
                      perc="...x..x....x..x.", shaker="x.x.x.x.x.x.x.x.", ghost="......o.......o.",
                      fills=["snare_roll", "drop"], gmd=["reggae"]),
    "reggae": dict(name="Reggae (one drop)", swing=0.12, human=0.6, roll="1/16T",
                   perc="..x...x...x...x.", shaker="xoxoxoxoxoxoxoxo", ghost="",
                   fills=["gmd", "snare_roll"], gmd=["reggae"]),
    # ---------------------------------------------------------------- african
    "afrobeats": dict(name="Afrobeats / afro-pop", swing=0.08, human=0.35, roll="1/16",
                      perc="..x..x..x..x.x..", shaker=".oxo.oxo.oxo.oxo", ghost="......o.......o.",
                      fills=["talking", "snare_roll", "drop"], gmd=["afrobeat", "highlife"]),
    "afrobeat": dict(name="Afrobeat (Fela)", swing=0.05, human=0.7, roll="1/16",
                     perc="x.xx.x.xx.x.x.x.", shaker="xoxoxoxoxoxoxoxo", ghost=".o..o..o.o..o..o",
                     fills=["gmd", "talking"], gmd=["afrobeat", "highlife"]),
    "amapiano": dict(name="Amapiano", swing=0.1, human=0.25, roll="1/32",
                     perc="...x..x...x..x..", shaker="xoxoxoxoxoxoxoxo", ghost="",
                     fills=["log_roll", "drop"], gmd=["afrobeat"]),
    # ---------------------------------------------------------------- rock / metal
    "rock": dict(name="Rock", swing=0.0, human=0.6, roll="1/16",
                 perc="", shaker="x.x.x.x.x.x.x.x.", ghost="......o.......o.",
                 fills=["gmd", "toms_down"], gmd=["rock"]),
    "punk": dict(name="Punk", swing=0.0, human=0.6, roll="1/16",
                 perc="", shaker="xxxxxxxxxxxxxxxx", ghost="",
                 fills=["gmd", "toms_down"], gmd=["punk"]),
    "metal": dict(name="Metal", swing=0.0, human=0.3, roll="1/32",
                  perc="", shaker="xxxxxxxxxxxxxxxx", ghost="",
                  fills=["blast", "toms_down"], gmd=["punk", "rock"]),
    "funk": dict(name="Funk", swing=0.12, human=0.7, roll="1/16",
                 perc="..x...x...x..x..", shaker="xoxoxoxoxoxoxoxo", ghost=".oo..o.o.oo..o.o",
                 fills=["gmd", "snare_roll"], gmd=["funk"]),
    "jazz": dict(name="Jazz / swing", swing=0.6, human=0.8, roll="1/8T",
                 perc="", shaker="x..x.xx..x.x", ghost="..o..o..o..o", steps=12,
                 fills=["gmd", "snare_roll"], gmd=["jazz"]),
    "gospel": dict(name="Gospel", swing=0.1, human=0.7, roll="1/32",
                   perc="", shaker="xoxoxoxoxoxoxoxo", ghost=".o.o.o...o.o.o..",
                   fills=["gmd", "toms_down", "chop"], gmd=["gospel", "soul"]),
    "neworleans": dict(name="New Orleans second line", swing=0.25, human=0.8, roll="1/16",
                       perc="x..x..x...x.x...", shaker="xoxoxoxoxoxoxoxo", ghost=".o.o.o.o.o.o.o.o",
                       fills=["gmd", "snare_roll"], gmd=["neworleans"]),
    # ---------------------------------------------------------------- electronic
    "house": dict(name="House / dance", swing=0.08, human=0.15, roll="1/16",
                  perc="..x..x....x..x..", shaker="xoxoxoxoxoxoxoxo", ghost="",
                  fills=["build", "snare_roll"], gmd=["dance"]),
    "techno": dict(name="Techno", swing=0.0, human=0.1, roll="1/16",
                   perc="...x...x...x...x", shaker="xxxxxxxxxxxxxxxx", ghost="",
                   fills=["build", "drop"], gmd=["dance"]),
    "dnb": dict(name="Drum & bass / breaks", swing=0.05, human=0.4, roll="1/32",
                perc="......x.......x.", shaker="xoxoxoxoxoxoxoxo", ghost=".o..o..o.o..o..o",
                fills=["chop", "snare_roll"], gmd=["dance/breakbeat", "funk"]),
    "jersey": dict(name="Jersey club", swing=0.0, human=0.1, roll="1/16",
                   perc="x..x..x.x.x.....", shaker="x.x.x.x.x.x.x.x.", ghost="",
                   fills=["kick_run", "drop"], gmd=["dance"]),
    "garage": dict(name="UK garage / 2-step", swing=0.3, human=0.25, roll="1/16",
                   perc="...x..x....x.x..", shaker="xoxoxoxoxoxoxoxo", ghost="..o.......o.....",
                   fills=["chop", "build"], gmd=["dance"]),
    # ---------------------------------------------------------------- world
    "middleeastern": dict(name="Middle Eastern (maqsum / baladi)", swing=0.0, human=0.6, roll="1/16",
                          perc="x..x..x.x..x..x.", shaker="xoxoxoxoxoxoxoxo", ghost=".o.o.o.o.o.o.o.o",
                          fills=["gmd", "darbuka"], gmd=["middleeastern"]),
    "bhangra": dict(name="Bhangra (dhol)", swing=0.25, human=0.5, roll="1/16",
                    perc="x.xx.xx.x.xx.xx.", shaker="xoxoxoxoxoxoxoxo", ghost="",
                    fills=["darbuka", "snare_roll"], gmd=["middleeastern"]),
    "highlife": dict(name="Highlife", swing=0.05, human=0.7, roll="1/16",
                     perc="x.x.xx.x.xx.x.x.", shaker="xoxoxoxoxoxoxoxo", ghost="",
                     fills=["gmd", "talking"], gmd=["highlife", "afrobeat"]),
    "country": dict(name="Country", swing=0.1, human=0.6, roll="1/16",
                    perc="", shaker="x.x.x.x.x.x.x.x.", ghost="",
                    fills=["gmd", "snare_roll"], gmd=["country"]),
    "pop": dict(name="Pop", swing=0.0, human=0.4, roll="1/16",
                perc="..x...x...x...x.", shaker="xoxoxoxoxoxoxoxo", ghost="",
                fills=["gmd", "toms_down", "build"], gmd=["pop"]),
}

ORDER = ["trap", "drill", "boombap", "hiphop", "rnb", "reggaeton", "corridos", "banda", "cumbia", "salsa",
         "bachata", "merengue", "samba", "afrocuban", "dancehall", "reggae", "afrobeats", "afrobeat", "amapiano",
         "highlife", "rock", "punk", "metal", "funk", "jazz", "gospel", "neworleans", "country", "pop", "house",
         "techno", "dnb", "jersey", "garage", "middleeastern", "bhangra"]


def auto_style(bpm, pads_present):
    """A sensible default from the song itself (Angel can change it)."""
    has808 = "kick2" in pads_present
    if has808:
        return "trap" if bpm >= 120 else "boombap"
    if bpm < 80:
        return "hiphop"
    if bpm < 100:
        return "reggaeton" if "perc1" in pads_present else "boombap"
    if bpm < 118:
        return "afrobeats"
    if bpm < 132:
        return "house"
    if bpm < 165:
        return "rock"
    return "dnb"
