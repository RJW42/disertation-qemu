from sys import stderr
from os import listdir
from os.path import isfile

import subprocess
import re
import timeit


PROGRAM_FOLDER = "./programs"
NUMBER_OF_RUNS = 1
QEMU_RUN_SCRIPT = "../build/qemu-aarch64 -L ~/Documents/arm-gnu-toolchain/aarch64-none-linux-gnu/libc"
VERSIONS = [-1, 0, 1, 2, 4]


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
            not file_name.endswith(".c") and \
            not file_name.endswith(".in")

    # Get list of all benchmarks
    progs = []

    for file in listdir(PROGRAM_FOLDER):
        if is_exec(file):
            progs.append(file)

    return progs


def benchmark(progs, version):
    # Benchmark all programs
    def benchmark_prog(prog, max_len):
        # Check if this prog has an input file
        in_file = "{}/{}.in".format(PROGRAM_FOLDER, prog)
        if not (isfile(in_file)):
            in_file = ""

        # Benchmark individual program
        times = []
        p_len = len(prog[len("benchmark-") + 2:])
        diff = max_len - p_len

        script = "{} {} {}/{} {}".format(
            QEMU_RUN_SCRIPT, 
            "-pt-trace {}".format(version) if version >= 0 else "" ,
            PROGRAM_FOLDER, prog, in_file
        ) 

        for i in range(0, NUMBER_OF_RUNS):
            # Record execution time
            start_time = timeit.default_timer()
            subprocess.run(
                script, shell=True, stdout=subprocess.DEVNULL
            )
            times.append(timeit.default_timer() - start_time)
        
        print(
            " Finished Program:", prog[len("benchmark-") + 2:], 
            " " * diff,
            "Avg Time:", sum(times) / NUMBER_OF_RUNS
        )

        return times

    results = {}

    max_len = 0
    for prog in progs: 
        p_len = len(prog[len("benchmark-") + 2:])
        if p_len > max_len:
            max_len = p_len

    print("Benchmarking Version ", version)
    for prog in progs:
        results[prog] = benchmark_prog(prog, max_len)

    return results


if __name__ == "__main__":
    main()