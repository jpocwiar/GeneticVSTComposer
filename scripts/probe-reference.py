"""Run the unmodified neighbouring generator; dependencies stay in build/reference-python.
Usage: PYTHONPATH=build/reference-python python3 scripts/probe-reference.py > build/audit-2.6/reference.csv
"""
import contextlib
import importlib.util
import io
from pathlib import Path
import random
import time
import numpy as np

path = Path(__file__).resolve().parents[2] / 'Synthesize-music-from-description/generation/evolutionary.py'
spec = importlib.util.spec_from_file_location('reference_evolutionary', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
print('type,seed,notes,notes_per_bar,pitches,ms,fitness_calls', flush=True)
for name, cls, mode, step in [('riff', module.GeneticBassGenerator, 'riff', .5),
                              ('motif', module.GeneticMotifGenerator, 'repeated', .25),
                              ('melody', module.GeneticMelodyGenerator, 'melody', .25)]:
    for seed in (1, 2):
        random.seed(seed)
        np.random.seed(seed)
        scale = [n for n in range(24, 108) if n % 12 in (0, 2, 3, 5, 7, 8, 10)]
        generator = cls(notes=[n for n in scale if 48 <= n <= 84], scale_notes=scale,
                        chords_dict={0:'Cm',4:'Fm',8:'Ab',12:'G7'}, mode=mode,
                        valence=.5, arousal=.5, note_duration=step,
                        population_size=64, num_generations=40)
        calls = [0]
        original = generator.fitness
        def fitness(notes):
            calls[0] += 1
            return original(notes)
        generator.fitness = fitness
        start = time.perf_counter()
        with contextlib.redirect_stdout(io.StringIO()):
            result = generator.run(measures=4)
        notes = [int(n) for n in result if n >= 0]
        print(f'{name},{seed},{len(notes)},{len(notes)/4},{len(set(notes))},{(time.perf_counter()-start)*1000:.3f},{calls[0]}', flush=True)
