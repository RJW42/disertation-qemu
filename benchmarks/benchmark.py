from sys import stderr
from os import listdir
from os.path import isfile

import subprocess
import re
import timeit


PROGRAM_FOLDER = "./programs"
NUMBER_OF_RUNS = 100
QEMU_RUN_SCRIPT = "../build/qemu-aarch64 -L ~/Documents/arm-gnu-toolchain/aarch64-none-linux-gnu/libc"
VERSIONS = [0, 1]


def main():
    progs = compile()
    results = gen_data(progs)


def save_data(results):
    # Save generated data to file
    pass


def gen_data(progs):
    # Generate all benchmark data 
    results = {}

    for version in VERSIONS:
        result = benchmark(progs, version)
        results[version] = result   
    
    return results


def compile():
    # Compile all benchmark programs 
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


def benchmark(progs, version):
    # Benchmark all programs
    def benchmark_prog(prog):
        # Benchmark individual program
        times = []

        for i in range(0, NUMBER_OF_RUNS):
            # Record execution time
            start_time = timeit.default_timer()
            subprocess.run(
                "{} -pt-trace {} {}/{}".format(
                    QEMU_RUN_SCRIPT, version, PROGRAM_FOLDER, prog
                ), 
                shell=True, stderr=subprocess.STDOUT
            )
            times.append(timeit.default_timer() - start_time)
        
        print(" Finished Program:", prog[len("benchmark-") + 2:])
        print("  Average Time:", sum(times) / NUMBER_OF_RUNS)

        return times

    results = {}

    print("Benchmarking Version ", version)
    for prog in progs:
        results[prog] = benchmark_prog(prog)

    return results


if __name__ == "__main__":
    main()