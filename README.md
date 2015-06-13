# tis100sim

Unofficial TIS-100 simulator

This currently simulates the removed "PRIME DETECTOR" and "SCATTER PLOT VIEWER" problems, for anyone who was working on them before they were removed. It also simulates the secret puzzle (which doesn't otherwise show cycle counts).

This is intended as a fast cycle counter to augment the TIS-100 game, and to make it easier to experiment with optimizations or automate solution verification. It is not intended as a game.

The simulator is written as a single translation unit for ease of compilation. It does not use any dynamic memory allocation, and it depends only on libc. It aims to be simple, concise, fast, and above all, accurate.


## Building

The simulator can be built using `make sim` (or `gcc sim.c -o sim -O3` or `clang sim.c -o sim -O3`).

MSVC is not yet supported, as I don't have the the environment set up to test it. It should only require only a handful of code tweaks (for `fopen_s` and the like).


## Usage

`./sim` will print usage, including the list of available puzzles.

`./sim <save file path>` will attempt to infer the puzzle from the filename and simulate it.

`./sim <save file path> <puzzle id>` will simulate the save file using the specified puzzle.
