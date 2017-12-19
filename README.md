# futures_cpp

A future and stream library for modern C++, inspired by
[futures-rs](https://github.com/alexcrichton/futures-rs).

## Intro

`futures_cpp` provides a full stack future and stream framework for
asynchronize event-driven programming:

* Future<T> & Stream<T> monand
* Core utils, ported from folly
* Proactors:
  - CPU Pool Exectutor
  - IO Executor, driven by `libev`
* Promise<T>, Timer, Timeout, Channel<T>
* IO Futures:
  - Async File
  - Async DNS Resolver
  - Pipe
  - TCP Server/Client Socket with SSL support
* Pipeline RPC Server/Client
  - HTTP/HTTPS
  - Websocket
  - Custom protocol
* Modules
  - readline
  - redis
  - mysql
  - zookeeper

`futures_cpp` use C++11 move semantic extensively, inspired by the `Rust`
programming.

## Build

`futures_cpp` support linux and macOSX.

### Compiler Support

* g++ 4.9 or later
* clang 3.6 or later

### Ubuntu 14.04

Install g++4.9 or later.

```bash
apt-get install libboost-dev libssl-dev
mkdir build
cd build && CXX=g++-4.9 CC=gcc-4.9 cmake -DENABLE_EXAMPLES=1 .. && make -j5
```

### macOSX

```bash
brew install boost openssl cmake
mkdir build
cd build && cmake -DENABLE_EXAMPLES=1 .. && make -j5
```

## Get Started

A minimun example:

```cpp
#include <iostream>
#include <futures/EventExecutor.h>
#include <futures/Timeout.h>
#include <futures/Stream.h>

using namespace futures;

int main(int argc, char *argv[]) {
  EventExecutor loop(true);
  auto print = nTimes(10).andThen([&loop] (int i) {
          std::cerr << "Timer: " << i << std::endl;
          return delay(&loop, 1.0);
    }).drop();
  loop.spawn(std::move(print));
  loop.run();
  return 0;
}
```

This example will run for 10 seconds, and 10 timers is fired _sequentially_.
All asynchronized events is represented as the `Future<T>` monand:

1. `nTimes(10)` create a `Stream<int>`, which iterator over 0 to 9.
2. `andThen()` iterate the stream, with lambda. 
3. `delay()` returns a Future<Unit>, which will be fullfiled after one second,
   in the meantime, the iterator will be scheduled out, and wait for timer
   event.
4. After the timer event fired, `andThen()` goes to the next iteration.

In `futures_cpp`, the statement is compiled to a single large state machine, which
is the `print` future.

Now we can run the future:

1. `loop.spawn()` will move `print` into the executor, and enqueue the task
   corresponding to `print` future.
2. `loop.run()` will run all tasks in the executor until no more tasks.

## Modules

Modules are optional IO drivers that implements `Future<T>` or `Stream<T>`
concepts, e.g. event-driven database drivers, network protocols, etc.

### MySQL

Event-driven MySQL DB driver based on mariadb-connector, with modern C++ interfaces and
connection pool.

### Redis

Event-driven redis driver based on hiredis.

### Zookeeper

Event-driven redis driver based on zookeeper C client.

### readline

Console module with line editing, based on `readline` or `editline`.

## Contribution

Fork & PR on Github.

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.

