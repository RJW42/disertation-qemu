from sys import stderr
from os import listdir
from os.path import isfile

import subprocess
import re
import timeit


PROGRAM_FOLDER = "./programs"
NUMBER_OF_RUNS = 1
QEMU_RUN_SCRIPT = "../build/qemu-aarch64 -L ~/Documents/Documents/arm-gnu-toolchain/aarch64-none-linux-gnu/libc -d nochain"


def main():
    progs = compile()
    results = benchmark(progs)

def compile():
    # Compile all benchmarks 
    subprocess.run(
        "cd {} && make clean all".format(PROGRAM_FOLDER),
        shell=True, stderr=subprocess.STDOUT
    )

    def is_exec(file_name):
        # Test if a file is a valid benchmark program
        file = "{}/{}".format(PROGRAM_FOLDER, file_name)
        return \
            isfile(file) and \
            re.search("benchmark-.*", file) and \
            not file_name.endswith(".c")

    # Get list of all benchmarks
    progs = []

    for file in listdir(PROGRAM_FOLDER):
        if is_exec(file):
            progs.append(file)

    return progs


def benchmark(progs):
    # Benchmark all programs
    def benchmark_prog(prog):
        # Benchmark individual program
        times = []

        for i in range(0, NUMBER_OF_RUNS):
            # Record execution time
            start_time = timeit.default_timer()
            subprocess.run(
                "{} {}/{}".format(QEMU_RUN_SCRIPT, PROGRAM_FOLDER, prog), 
                shell=True, stderr=subprocess.STDOUT
            )
            times.append(timeit.default_timer() - start_time)
        
        print("Finished Program: ", prog)
        print(" Average Time: ", sum(times) / NUMBER_OF_RUNS)

        return times

    results = {}

    for prog in progs:
        results[prog] = benchmark_prog(prog)

    return results




if __name__ == "__main__":
    main()