# Wait-free Binary tree
This is an implementation of the wait-free binary tree proposed by Kokorin, Yudov, Vitaly & Alistarh\[[1](https://ieeexplore.ieee.org/document/10579241)\]. The first version of this implementation was created during a practical course in university.

The project uses `boost::atomic` as I didn't get `std::atomic` to work with 128bit types. Make sure to install boost before configuring cmake.

### Problems
The performance is quite bad at the moment. See `eval/plots.pdf` for the results of the benchmarks run on a Ryzen 7 2700 and 16GB of RAM. The operations per second are more than one order of magnitude worse than the ones in the original paper.

The tree node deallocation is probably not wait-free at the moment. All subtrees that are removed from the tree are pushed to a queue. After a thread finishes an operation, it iterates over the whole queue and marks all subtrees as not used by this thread. If a subtree is not in use by any thread, it is deallocated. The number of subtrees in the queue is not bounded. This could therefore destroy the wait-freedom. There might be an amortization argument to be made but I am not sure if that works.

### Setup
Run `cmake --preset release` to configure cmake. 
To generate the benchmark plots, R with the tidyverse package is used.
Run `cmake --build build-release -t plots` to generate a pdf with plots like the one in the eval directory.

### Acknowledgements
Some implementation tricks for the wait-free queue have been taken from [this](https://concurrencyfreaks.blogspot.com/2016/12/a-c-implementation-of-kogan-petrank.html) blog post by the Concurrency Freaks.
Additionaly the HazardPointers and ConditionalHazardPointers classes are adaptions of their HazardPointers class.