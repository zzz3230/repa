# Repa

An in-memory key-value server written in C. It speaks the Redis protocol (RESP), so `redis-cli` and `redis-benchmark` work with it out of the box, and it runs on a coroutine runtime written from scratch.

Repa started as a university operating systems project at Novosibirsk State University. It first used a pthread worker pool and was later moved to coroutines.

## Features

- RESP parser that handles commands split across any number of TCP reads
- Coroutine runtime built on `ucontext`, with its own scheduler and synchronization primitives
- Network loop and request workers that run as coroutines in a single OS thread
- Custom allocator over a fixed memory area, with key eviction when the area is full
- Graceful shutdown on `SIGINT` and `SIGTERM` through `signalfd` and `eventfd`

## Quick start

Repa runs on Linux and needs CMake 3.29 or newer, a C23 compiler, and GLib. On Debian 13:

```bash
sudo apt install gcc make cmake pkg-config libglib2.0-dev redis-tools
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

The build copies `repa.conf` next to the binary. Start the server from the build directory:

```bash
cd build && ./repa_server
```

Then connect from another terminal:

```console
$ redis-cli -p 6379 SET foo bar
OK
$ redis-cli -p 6379 GET foo
bar
$ redis-cli -p 6379 DEL foo
OK
```

## Commands

| Command | Reply | Difference from Redis |
| --- | --- | --- |
| `PING` | `pong` | Lowercase reply; no message argument |
| `SET key value` | `OK` | No options such as `EX` or `NX` |
| `GET key` | Value | A missing key returns an error, not nil |
| `DEL key` | `OK` | One key only; returns `OK`, not a count |
| `AUTH user password` | `OK` | Takes both arguments; see [Limitations](#limitations) |
| `STATS` | Array of counters | Repa-specific |

## Configuration

`repa.conf` uses `key = value` lines. The server reads these settings:

| Key | Default in `repa.conf` | Meaning |
| --- | --- | --- |
| `port` | `6379` | TCP port |
| `workers` | `8` | Number of worker coroutines |
| `max_memory_byte` | `536870912` | Size of the storage area in bytes (512 MB) |
| `default_user`, `default_password` | `admin`, `admin` | Credentials checked by `AUTH` |

Change the default credentials before exposing the server to a network.

## Architecture

```text
             TCP clients
                  │
                  ▼
        ┌───────────────────┐
        │  listener         │  polls sockets, signalfd, eventfd
        │  coroutine        │  feeds bytes to each client's RESP parser
        └─────────┬─────────┘
                  │ commands go to a per-client queue
                  ▼
        ┌───────────────────┐
        │  worker           │  one client's commands stay in order
        │  coroutines       │  runs the command and writes the reply
        └─────────┬─────────┘
                  ▼
        ┌───────────────────┐
        │  storage          │  hash index + fixed memory area
        │                   │  background coroutine merges free blocks
        └───────────────────┘

   All coroutines run in one OS thread under one scheduler.
```

### Coroutine runtime

The code is in [`coroutine_pool/`](coroutine_pool).

- Each coroutine has its own heap-allocated stack, 64 KB by default, and switches with `swapcontext`.
- The scheduler keeps a queue of ready coroutines. When a coroutine waits on a file descriptor or a timer, the scheduler wakes it through a single `poll` call.
- The blocking calls are `cor_poll`, `cor_sleep_ms`, and `cor_yield`.
- The synchronization primitives are a mutex, a semaphore, a read-write lock, and wait/notify on a token. All of them are cooperative and suspend only the calling coroutine.

### Storage

The code is in [`db/db_backend.c`](db/db_backend.c).

- A hash table maps each key to an index entry that holds the value's offset and size in the memory area.
- New values are placed at the top of the used area. Free blocks are kept in a tree ordered by offset, and a background coroutine merges adjacent blocks.
- When no block is large enough, the server evicts keys from the start of the area until the value fits.

## Performance

`redis-benchmark -t set,get -n 100000 -c 50`, Release build, in a Docker container on Windows:

| Operation | Requests per second | p50 latency |
| --- | --- | --- |
| `SET` | 22,538 | 1.12 ms |
| `GET` | 22,046 | 1.03 ms |

These numbers come from one run on a desktop machine, so treat them as a rough reference.

## Tests

The coroutine runtime has two test suites in `coroutine_pool/`:

- `coro_super_tests.c` covers creation, yielding, each synchronization primitive, sleeping, `poll`, nested coroutines, stress runs, and error cases.
- `coro_more_test.c` adds stress runs with many coroutines, repeated scheduler creation, and custom stack sizes.

`coro_test.c` in the same folder is an early demo, not a test suite.

The entry points are renamed so they do not clash with the server's `main`. To build the main suite on its own:

```bash
gcc -std=gnu2x -D_GNU_SOURCE -D______main=main coroutine_pool/coroutine_pool.c coroutine_pool/coro_super_tests.c logger.c -o coro_tests $(pkg-config --cflags --libs glib-2.0)
```

## Limitations

- Only the commands listed above are implemented, and only string values.
- Data lives in memory only and is lost on restart.
- Key expiry is not implemented.
- `AUTH` checks credentials, but commands do not require it because `SKIP_AUTH` is set to `true` in `db/command_processor.c`.
- `STATS` currently reports zero for its counters.
- The server uses one OS thread, so it does not scale across CPU cores.
- `poll` is rebuilt for every loop iteration, so it costs more as the number of clients grows.
