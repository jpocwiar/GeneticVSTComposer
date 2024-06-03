import matplotlib.pyplot as plt
import os

if __name__ == "__main__":
    os.makedirs("./graphs", exist_ok=True)
    file_name = "fitness_512.txt"
    base_file_name = os.path.splitext(file_name)[0]  # Get the base filename without extension
    with open('./fitness/' + file_name, 'r') as file:
        lines = file.readlines()

    # Parse data
    generations = []
    avg_fitness = []
    min_fitness = []
    max_fitness = []

    for line in lines[1:]:  # Skip the header
        data = line.split()
        generations.append(int(data[0]))
        avg_fitness.append(float(data[1]))
        min_fitness.append(float(data[2]))
        max_fitness.append(float(data[3]))

    # Plot
    plt.figure(figsize=(10, 6))

    # Plot average fitness
    plt.plot(generations, avg_fitness, label='Average Fitness', color='blue')

    # Plot shaded region for min and max fitness
    plt.fill_between(generations, min_fitness, max_fitness, color='blue', alpha=0.2)

    # Add labels and title
    plt.xlabel('Generation')
    plt.ylabel('Fitness')
    plt.title('Fitness Change Over Generations\nfor population size = 512')
    plt.legend()

    # Show plot
    plt.grid(True)
    plt.gcf().set_size_inches(10, 6)
    plt.savefig(f'./graphs/{base_file_name}.jpg', format='jpg', dpi=300)
    plt.close()