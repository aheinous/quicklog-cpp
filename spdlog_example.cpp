#include "quicklog.h"
#include <mutex>
#include <array>
#include <time.h>

#include <pthread.h>
#include <semaphore.h>

#include "spdlog/spdlog.h"


/**
 * @file spdlog_example.cpp
 * 
 * @brief Example implementation 
 * 
 * Compile with @code {.sh}
 * g++ -Wall -std=c++14 -O2 -Ispdlog/include spdlog_example.cpp '-DQUICKLOG_PRINT=spdlog::info' -'DQUICKLOG_INCLUDE="spdlog/spdlog.h"' -pthread 
 * @endcode 
 * 
 * To see times run: @code{.sh}
 * ./a.out | grep times -A 2
 * @endcode
 */

using std::chrono::high_resolution_clock;



/**
 * @brief Example of user supplied platform specific details.
 * 
 */
class PthreadPlatformImpl{
public:
    PthreadPlatformImpl(){
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



quicklog::LogServer<4, PthreadPlatformImpl> g_server;



class LogProducer{
public:
    LogProducer(const char*name): m_name(name){}

    void start(){   
        pthread_create(&m_thread, NULL, call_process, this);
    }

    void join(){
        pthread_join(m_thread, nullptr);
    }

private:
    void process(){
        g_server.addLogger(m_logger);

        std::chrono::nanoseconds quicklog_time(0);
        std::chrono::nanoseconds spdlog_time(0);


        for(int i = 0; i< 1024; i++){

            auto a = high_resolution_clock::now();

            m_logger.log("ql[{}] n: {}", m_name, i);

            auto b = high_resolution_clock::now();
            
            spdlog::info("sl[{}] n: {}", m_name, i);

            auto c = high_resolution_clock::now();
 
            quicklog_time += (b-a);
            spdlog_time += (c-b);
        }

        spdlog::critical("times: {}\n\tquicklog: {} us\n\tspdlog {} us", m_name, 
            std::chrono::duration_cast<std::chrono::microseconds>(quicklog_time).count(), 
            std::chrono::duration_cast<std::chrono::microseconds>(spdlog_time).count()    
        );
        m_logger.flush();
    }

    static void* call_process(void * self){
        static_cast<LogProducer*>(self)->process();
        return nullptr;
    }

    const char *m_name = nullptr;
    quicklog::LocalLogger<8, 16*1024> m_logger;
    pthread_t m_thread;
};



    std::array<LogProducer, 4>  producers{
        LogProducer("a"),
        LogProducer("b"),
        LogProducer("c"),
        LogProducer("d"),   
    };


int main(){
    pthread_t serverThread;
    pthread_create(&serverThread, NULL, g_server.process, &g_server);


    for(auto& p: producers){
        p.start();
    }

    for(auto& p: producers){
        p.join();
    }

    g_server.shutdown();
    pthread_join(serverThread, NULL);

    printf("DONE\n");
}