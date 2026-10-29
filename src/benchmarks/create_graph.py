import matplotlib.pyplot as plt
import json

filenames = input("Filenames to parse: ")
filenames = filenames.split(" ")
# filenames = ["benchmark_results_cxxbus.json", "benchmark_results_sdbus.json"]
data: dict = {}

names = []
times = []

for filename in filenames:
    with open(filename, "r") as f:
        data = json.loads(f.read())

    names.extend(
        benchmark["name"][benchmark["name"].find("_") + 1 : benchmark["name"].find("/")]
        for benchmark in data["benchmarks"]
        if "mean" in benchmark["name"]
        and benchmark["name"][
            benchmark["name"].find("_") + 1 : benchmark["name"].find("/")
        ]
        not in names
    )
    times.append(
        [
            benchmark["real_time"]
            for benchmark in data["benchmarks"]
            if "mean" in benchmark["name"]
        ]
    )

fig, ax = plt.subplots(figsize=(12, 6))


nr_of_files = len(filenames)
list_range = list(range(len(names)))
width_per_bar = 0.8 / nr_of_files

for i in range(len(filenames)):
    offset = (i - (nr_of_files - 1) / 2) * width_per_bar
    positions = [pos + offset for pos in list_range]
    bar = ax.bar(
        positions, times[i], width_per_bar, label="CxxBus" if i == 0 else "SDBus"
    )
    ax.bar_label(
        bar,
        labels=[f"{t:.0f} ns" for t in times[i]],
        padding=2,
        fontsize=7,
    )

ax.set_xticks(list_range)
ax.set_xticklabels(names, rotation=45, ha="right")
ax.set_ylabel("Real time (nanoseconds)")
# ax.set_yscale("log")
ax.legend(title="CxxBus vs SDBus benchmarks")
plt.setp(ax.get_xticklabels(), rotation=45, ha="right")

fig.tight_layout()
fig.savefig("results.png")
