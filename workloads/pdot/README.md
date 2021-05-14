# pdot

## What is this?
pdot computes the dot product of two vectors of uint64_t type.

## How does it work?
pdot reads input from a file, copy it to shared memory and spawns `n`
threads. Each thread computes dot-product of 1/n th part of the
vector, stores the result in a designated result area. The manager
then sums up all the individual results and prints it to stdout.

## Usage
```
./pdot <input-file>
```

First line of `input-file` holds the number of elements (k). The next
k/2 lines are the elements of the first vector and the last k/2 lines
are the elements of the second vector.
