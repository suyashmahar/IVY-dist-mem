# psort
Spawns n threads and sorts 1/nth of the data using each thread. All the data is stored in a single shared memory. At the completion of the individual sort operation, the manager thread merges the result of each worker and writes it to the stdout.

Usage:  
```shell
psort <input-file>
```

The first line of `input-file` contains the total number of elements (n). The `n` subsequent lines then contain a single number that can be represented as `uint64_t`.
