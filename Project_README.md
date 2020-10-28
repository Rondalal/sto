# Adding HOT Index to STO Framework

## Compiling both projects together
The original way to build STO (and Masstree) is with configure files. However, the HOT index was meant to compile using Cmake files. Therefore, a lot of time was spent creating cmake files in order to compile both projects together.
I've chosen Cmake because HOT has more third-party projects that compile only using cmake. 

Hence, in order to compile the bechmarks and the tests - one can use the Clion IDE (most recommended), or Cmake cli

## Implemented Benchmarks
All the benchmarks support changing the index (between Masstree and HOT)
  - TPCC - a widely accepted and comprehensive benchmark, that includes a range scan, and tries to mimic a read-world scenario
- micro_bench - general benchmark that was recommended by the authors of STO, and referenced in the paper

All the benchmarks support changing the index (between Masstree and HOT)
However, since the benchmarks didn’t use dynamic allocation for the keys and the values that had been inserted to the index, it could cause problems with the HOT index (since the variables might be overwritten after the functions are called). Therefore, I changed the static allocated keys and values to be dynamically allocated.

# Major modifications
## STO Changes
#### Changes in `DB_oindex`
In order to add another index to STO framework, one needs to implement an index class inside `DB_oindex.hh` file.
Thus, most of the changes are implemented in that file.
The index class that I implemented is called `hot_index` and consist with the interface of ordered_index (masstree) and MVCC:
 - `select_row`
 - `insert_row`
 - `update_row`
 - `nontrans_put/get` (non transaction insert / select - only one thread is calling those functions)
 - `cleanup`
 - `delete_row`
 - `range_scan`

I have used the `internal_elem` object, found in the original implementation of ordered_index, as the value that is stored in each of HOT elements. Most of the versioning happens inside internal_elem.

The non-trivial implentations has been made in the following functions:

##### `insert_row`
Although the insert isn’t supposed to be called with keys that are already stored in the tree (it is documented in the original code of STO), it happens.
Thus, I added a check in HOT trie insert implementation (more on that change in the "HOT changes" section) and inside the `insert_row` function, to treat calls of already present keys.

##### `delete_row`
Due to the fact that the concurrent version of HOT doesn't support delete from the trie itself, I have implemented a workaround, and marked the stored element as deleted

##### `range_scan` 
Since the key to begin the scan with might not be found in the index, I have used the lookup function to find the closest key. After getting an iterator from the closest key, the function will go over the keys, until it will reach one of the following states:
- The number of iterations will reach its limit (hence the limit variable)
- The iterator will come to the end of the index (last inter_elem in the HOT trie)
- The iterator will pass the end key (i.e - the iterator current key will be 5 and the end key will be 4)
- The defined callback will return false

Furthermore, because the deletion is not fully implemented in the HOT trie, if in the scan the loop meets with an element that is "deleted" (== marked as deleted), it will continue, without incrementing the number of iterations

## HOT Changes
Most of the HOT changes were due to requirements in the STO framework

##### Adding Key Extractor
One of the most fundamental ideas in HOT is the efficient node representation, thus, each inserted value is a pointer to an object, that a key is getting extraced from it
Therefore, in order to add a support for new object to be indexed inside the HOT trie - I implemented a new key extractor, that extract the key from `internal_elem`

##### Supporting insert
Sometimes the insert function was called, despite the key was already inserted. That's why I added a HOTChildPointer that points to the already exists value


## Reproducing Results

#### Using HOT Index
In order to compile the relevant benchmark, it is most recommended to use the Clion IDE. Clion will scan the Cmake files and create configuration for each of the available executables to compile. 

The benchmarks that I have used are `micro_bench` and `tpcc_bench`. I have also ran the unit-dboindex test, and other HOT tests, in order to make sure that everything still works.
I ran each benchmark from 1 to 60 threads (with steps of 2 threads each time), and for each step the test ran 15 times (in order to get a decent average and standard deviation).
Furthermore, in order to comply with the STO paper, I ran each of the tests for 20 seconds (as in the paper), and used the other default parameters (for each benchmark).

The pseudo code for each benchmark was:
```
for i in {1..60} 
    for j in {1..15}
        ./benchmark --threads=i --time=20 --dbid=tictoc/default(OCC) >> save_to_file // each of the benchmarks ran 20 seconds
```

#### Using Masstree
I ran STO in 2 different modes:
* "out of the box" - compiled and ran exactly as written in the [Github] page. 
* In the STO modified version
Using the benchmarks mentioned above (tpcc and micro). 
Both benchmarks were ran with the exact parameters that I have used in the previous section (threadcount, time, etc.)

[Github]: <https://github.com/readablesystems/sto>