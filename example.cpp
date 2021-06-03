#include "quicklog.h"
#include <thread>
#include <mutex>
#include <array>
#include <time.h>

/**
 * @file example.cpp
 * 
 * @brief Example implementation using printf and threads from the C++ standard library.
 * 
 * Compile with @code {.sh}
 * g++ -Wall -std=c++14 -O2 example.cpp -pthread 
 * @endcode
 * 
 * To see execution times run: @code{.sh}
 * ./a.out | grep times -A 3
 * @endcode
 * 
 * 
 */

using std::chrono::high_resolution_clock;



/**
 * @brief Example of user supplied platform specific details.
 * 
 */
class StdLibPlatformImpl{
public:

    /**
     * @brief Server waits in some way
     * Should either be call to thread yield function or similar,
     * or
     * should be a call to a semaphore-get function, and notify is a call to 
     * to a semaphore-put function. 
     */
    void wait(){
        std::this_thread::yield();
    }


    /**
     * @brief server is notified of logs being available to print.
     * 
     */
    void notify(){

    }

    /**
     * @brief  A PlatformImpl should contain a mutex. Lock that mutex.
     * 
     */
    void lock(){
        mutex.lock();
    }

    /**
     * @brief  A PlatformImpl should contain a mutex. Unlock that mutex.
     * 
     */
    void unlock(){
        mutex.unlock();
    }
private:
    std::mutex mutex;
};



quicklog::LogServer<4, StdLibPlatformImpl> g_server;



class LogProducer{
public:
    LogProducer(const char*name): m_name(name){}

    void start(){   
        m_thread = std::thread(call_process, this);
    }

    void join(){
        m_thread.join();
    }

private:
    void process(){
        g_server.addLogger(m_logger); // this needs to be called from this thread.


        char buffer[128];

        std::chrono::nanoseconds quicklog_time(0);
        std::chrono::nanoseconds printf_time(0);
        std::chrono::nanoseconds snprintf_time(0);


        for(int i = 0; i< 1024; i++){

            auto a = high_resolution_clock::now();
            m_logger.log("ql[%s] n: %d\n", m_name, i);

            auto b = high_resolution_clock::now();
            printf("pf[%s] n: %d\n", m_name, i);

            auto c = high_resolution_clock::now();
            volatile int n = snprintf(buffer, sizeof(buffer), "sn[%s] n: %d\n", m_name, i);
            (void)n;

            auto d = high_resolution_clock::now();
            quicklog_time += (b-a);
            printf_time += (c-b);
            snprintf_time += (d-c);
        }

        m_logger.log("times: %s \n\tquicklog: %ld us\n\tprintf: %ld us\n\tsnprintf: %ld us\n", m_name, 
            std::chrono::duration_cast<std::chrono::microseconds>(quicklog_time).count(), 
            std::chrono::duration_cast<std::chrono::microseconds>(printf_time).count(),
            std::chrono::duration_cast<std::chrono::microseconds>(snprintf_time).count()
            
            );
        m_logger.flush();
    }

    static void call_process(LogProducer * self){
        self->process();
    }

    const char *m_name = nullptr;
    quicklog::LocalLogger<8, 16*1024> m_logger;
    std::thread m_thread;
};



    std::array<LogProducer, 4>  producers{
        LogProducer("a"),
        LogProducer("b"),
        LogProducer("c"),
        LogProducer("d"),   
    };


int main(){
    std::thread serverThread(g_server.process, & g_server);


    for(auto& p: producers){
        p.start();
    }

    for(auto& p: producers){
        p.join();
    }

    g_server.shutdown();
    serverThread.join();

    printf("DONE\n");
}