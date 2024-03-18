from genetic import GeneticMelodyGenerator

generator = GeneticMelodyGenerator("A Minor", ('A-3', 'A-7'))
coefficients_values = {
    'diversity': 0.85,
    'diversity_interval': 0.75,
    'dissonance': 0.2,
    'rhythmic_diversity': 0.65,
    'rhythmic_average_value': 0.55,
    'very_long_notes_score': 0.05,
    'deviation_rhythmic_value': 0.45,
    'scale_conformance': 0.85,
    'chord_conformance': 0.45,
    'melodic_contour': 0.15,
    'pitch_range': 0.35,
    'pause_proportion': 0.25,
    'large_intervals': 0.05,
    'average_pitch': 0.65,
    'pitch_variation': 0.45,
    'odd_index_notes': 0.15,
    'average_interval': 0.35,
    'scale_playing': 0.85,
    'short_consecutive_notes': 0.8
}

# Wywołanie metody set_coefficients z powyższymi wartościami jako argumentami
generator.set_coefficients(**coefficients_values)
melody = generator.run(1)

print(melody)