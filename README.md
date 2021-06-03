# Quicklog

Quicklog is a logging utility for timing-critical code. It was designed with the goal of having the absolute bare-minimum latency on the producer side while still being reasonably flexible. Quicklog is not a complete logging system, but it does provide the ability to forward calls to basically any other logger you'd like.

## Features
- Absolutely no locking or calls to snprintf etc. on the producer side.
- No dynamic memory allocation.
- Not dependent on any particular threading library/platform.
- Contained in a single header file.
- C++ 14


## Examples
See example.cpp for an example that uses printf, and threads from the standard library.

See spdlog_example.cpp for an example that uses spdlog and pthreads.

## Documentation
Run 
```sh
doxygen 
```
to generate documentation.

## Overview

Each thread that wishes to produce log messages should have it's own LocalLogger
```cpp
quicklog::LocalLogger<N_BUFFERS, BUFFER_SIZE> m_logger;
```
Each LocalLogger will have N_BUFFERS byte buffers of size BUFFER_SIZE. When a buffer fills up the server prints it and the localLogger moves to the next one.

Each LocalLogger should be registed to thr LogServer by it's corresponding thread
```cpp
g_server.addLogger(m_logger);
```
Log entries are created by calls to quicklog::LocalLogger::log(), which takes arbitrary arguments as long as they are copyable.
```cpp
m_logger.log("this is a log msg.\n");
```
You'll need to create a LogServer and start it's thread.
```cpp
quicklog::LogServer<MAX_LOCAL_LOGGERS, ExamplePlatformImpl> g_server;
// ...
std::thread serverThread(g_server.process, & g_server);
```
and provide platform specific functionality. Like
```cpp
class ExamplePlatformImpl{
public:
    ExamplePlatformImpl(){
        sem_init(&m_semaphore, 0, 0);
    }
    void wait(){
        sem_wait(&m_semaphore);
    }
    void notify(){
        sem_post(&m_semaphore);
    }
    void lock(){
        pthread_mutex_lock(&m_mutex);
    }
    void unlock(){
        pthread_mutex_unlock(&m_mutex);
    }
private:
    pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
    sem_t m_semaphore;
};
```
 -or-
 ```cpp
 class ExamplePlatformImpl{
public:
    void wait(){
        std::this_thread::yield();
    }
    void notify(){}
    void lock(){
        mutex.lock();
    }
    void unlock(){
        mutex.unlock();
    }
private:
    std::mutex mutex;
};
```

See quicklog.h for things you can #define to change its behaviour.


