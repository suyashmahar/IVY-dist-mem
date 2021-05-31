# Distributed shared virtual memory 
Using IVY's fixed ownership model.

## Project structure
1. [analysis](src/analysis/): Memory trace visualization
2. [libivy](src/libivy/): IVY implementation as a shared object
3. [workloads](src/workloads/): Parallel workloads written in C++
   1. [psort](src/workloads/psort): Parallel list sort
   2. [pdot](src/workloads/pdot): Parallel vector dot-product
   3. [pingpong](src/workloads/pingpong): Page ping-pongs between two nodes
   4. [pagefaultlatency](src/workloads/pagefaultlatency): Calculates the cost of read and write page faults
   
## API
**Note:** libivy only supports C++20

```cpp

// Create an object of the class `IVY` with the config file and node ID
Ivy ivy("path/to/config", id);

// Automatically sets up the shared memory and all the handlers
auto [shm, err] = ivy.get_shm(); 

if (err.has_value()) throw std::runtime_error(err.value());

auto *ul_arry = reinterpret_cast<uint64_t*>(shm);

// Write to the shared region
ul_array[0] = 0xDEADBEEF;

// Unmount the shared memory
ivy.drop_shm();
```
