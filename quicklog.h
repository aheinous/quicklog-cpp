#pragma once

/**
 * @file quicklog.h
 * 
 * All definitions.
 * 
 */

#include <cstring>
#include <cstdint>
#include <cstddef>
#include <tuple>



/**
 * @def QUICKLOG_INCLUDE
 */
/**
 * @brief Define this to include any extra headers required by QUICKLOG_PRINT
 * or QUICKLOG_ERROR
 * 
 */
#ifndef QUICKLOG_INCLUDE
#define QUICKLOG_INCLUDE <cstdio>
#endif

#include QUICKLOG_INCLUDE



/**
 * @def QUICKLOG_PRINT
 */
/**
 * @brief  The print function that will ultimately be called via LocalLogger::log().
 * 
 * 
 * Defaults to printf.
 * 
 */
#ifndef QUICKLOG_PRINT
#define QUICKLOG_PRINT(...) printf(__VA_ARGS__)
#endif


/**
 * @def QUICKLOG_ERROR(...)
 * 
 */
/**
 * @brief Error handling macro called by quicklog. Takes a const char*.
 * Defaults to
 * \code{.cpp}
 * do{ fprintf(stderr, "ERROR: " __VA_ARGS__); exit(1); } while(0)
 * \endcode
 */
#ifndef QUICKLOG_ERROR
#define QUICKLOG_ERROR(...) do{ fprintf(stderr, "ERROR: " __VA_ARGS__); exit(1); } while(0)
#endif



#define QUICKLOG_ALIGN (alignof(std::max_align_t))



#define QUICKLOG_MEMORY_FENCE asm volatile ("" : : : "memory")

/**
 * @brief main namespace
 * 
 */
namespace quicklog{


/**
 * @brief Private implementation details.
 * 
 */
namespace detail {

    class LogServerBase{
    public:
        virtual void _onDumpAvail() = 0;
    };

    class LocalLoggerBase{
    public:
	    virtual bool dump() = 0;
    };

    class LogEntryBase{
    public:
        size_t size;
        LogEntryBase(size_t size) : size(size){}

        virtual void printSelf() = 0;
    };


    /**
     * @brief Special purpose semaphore implementation.
     * 
     * This isn't a normal semaphore. It only works if only one thread is allowed to
     * call put() and only one thread is allowed to call get()/peek(). It relies only on the atomicity
     * uint8_t reads and writes, and is therefor independent of platform or available libraries.
     * 
     */
    class semaphore{
    public:
        void put(){
            numPuts++;
        }

        /**
         * @brief
         * 
         * @return uint8_t Current semaphore count.
         */
        uint8_t peek(){
            return numPuts-numGets;
        }

        /** 
         * @brief Decrement semaphore count. Assume peek was called already and
         * decrement without checking.
         */
        void get(){
            if(numPuts == numGets){
                QUICKLOG_ERROR("get called on non-gettable semaphore.\n");
            }
            ++numGets;
        }

    private:
        volatile uint8_t numPuts = 0;
        volatile uint8_t numGets = 0;
    };


    template<typename Tuple, size_t ... I>
    auto callPrintFunc(Tuple t, std::index_sequence<I ...>){
        return QUICKLOG_PRINT(std::get<I>(t) ...);
    }


    template<typename Tuple>
    auto callPrintFunc(Tuple t){
        constexpr auto size = std::tuple_size<Tuple>::value;
        return callPrintFunc(t, std::make_index_sequence<size>{});
    }


    /**
     * @brief Round sz up to size required for alignment.
     * 
     * @param sz 
     * @return constexpr size_t 
     */
    constexpr size_t alignedSize(size_t sz){
        if(sz % QUICKLOG_ALIGN){
            sz += (QUICKLOG_ALIGN - sz % QUICKLOG_ALIGN);
        }
        return sz;
    }



    template<typename ... Ts>
    class LogEntry : public LogEntryBase{
    public:
        std::tuple<Ts ...> values;

        LogEntry() : LogEntryBase(0) {}

        LogEntry(Ts ... args) : LogEntryBase(sizeof(*this))
                                                , values(std::make_tuple(args ...))
        {}

        virtual void printSelf(){
            callPrintFunc(values);
        }
    };


    template <size_t size>
    class EntryBuffer{
    public:

        template<typename ...Ts>
        bool pushElem(const LogEntry<Ts ...> elem){
            constexpr size_t align_sz = alignedSize(sizeof(elem));

            if(align_sz + m_pos > size){
                return false;
            }

            // this will break if used with multiple inheritance
            auto dest = reinterpret_cast< LogEntry<Ts ...>*>( &m_buffer[m_pos] );
            new (dest) LogEntry<Ts ...>(); // vtable
            *dest = elem;

            m_pos += align_sz;
            m_count ++;
            return true;
        }


        void clear(){
            m_count = 0;
            m_pos = 0;
        }

        bool isEmpty(){
            return m_pos == 0;
        }

        void dump(){
            size_t dump_pos = 0;
            for(size_t i=0; i<m_count; i++){
                LogEntryBase * entry = reinterpret_cast<LogEntryBase*> (& m_buffer[dump_pos]);
                entry->printSelf();
                dump_pos += alignedSize(entry->size);
            }
            clear();
        }
    private:
        size_t m_count = 0;
        size_t m_pos = 0;
        uint8_t m_buffer[size] alignas(QUICKLOG_ALIGN);
    };



}; // detail

using namespace detail;

/**
 * @brief Thread-local logger component.
 * 
 * Stores log entries without processing them in anyway. Calls to \ref log() will effectively
 * result the arguments being memcpy'd into a buffer (ie: buffer). When the buffer fills up 
 * LogServer will "dump" the results and call #QUICKLOG_PRINT on the original arguments to \ref log().
 * By default #QUICKLOG_PRINT is printf, but it can be #define'd  as any function you want, with any type-signature,
 * as long as it matches the corresponding call to \ref log().
 * 
 * @tparam numBuffers The number of buffers.
 * @tparam bufferSize The size of the buffers in bytes.
 */
template<size_t numBuffers, size_t bufferSize>
class LocalLogger: public LocalLoggerBase
{
public:
    /**
     * @brief Submit a log message.
     * 
     * Will not block or make any calls to snprintf etc.
     * 
     * @tparam Ts arbitrary types.
     * @param vs arbitrary values.
     */
    template <typename ...Ts>
    void log(Ts ... vs){
        if(full()){
            QUICKLOG_ERROR("LocalLogger full. Can't log()\n");
            return;
        }

        LogEntry<Ts...> ent(vs...);
        
        bool success = buffers[writeIndex].pushElem(ent);
        if(!success){
            nextIndex();
            success = buffers[writeIndex].pushElem(ent);
            if(!success){
                QUICKLOG_ERROR("Log entries bigger than buffer size\n");
            }
        }
    }

    /**
     * @brief Flush the current buffer.
     * 
     *  Flushes the current buffer and makes it available to be "dumped" by the LogServer.
     *  If this function is never called, and there are no more calls to \ref log(), more recent 
     *  log entries will never be printed. 
     */
    void flush(){
        if(!buffers[writeIndex].isEmpty()){
            nextIndex();
        }
    }

private:
    virtual bool dump(){
        if(buffersFull.peek()){
            // don't get() from buffersFull until after we've dump()'d the buffer.
            QUICKLOG_MEMORY_FENCE;
            buffers[readIndex].dump();
            ++readIndex;
            QUICKLOG_MEMORY_FENCE;
            buffersFull.get(); //guaranteed to succeed
            return true;
        }
        return false;
    }


    bool full(){
        return buffersFull.peek() == numBuffers;
    }


    void nextIndex(){
        if(!full()){
            writeIndex = (writeIndex+1) % numBuffers;
            buffersFull.put();
            if(server == nullptr){
                QUICKLOG_ERROR("LocalLogger not registered to LogServer\n");
            }else{
                server->_onDumpAvail();
            }
        }else{
            QUICKLOG_ERROR("localLogger full, can't nextIndex()");
        }
    }

    EntryBuffer<bufferSize> buffers[numBuffers];
    volatile uint8_t writeIndex = 0;
    volatile uint8_t readIndex = 0;
    semaphore buffersFull;
    LogServerBase * server = nullptr;

    template<size_t maxLoggers, class PlatformImpl>
    friend class LogServer;
};


/**
 * @brief Server responsible for managing LocalLogger instances and performing actual printing.
 * 
 * @tparam maxLoggers Maximum number of LocalLogger instaces that can be registered.
 * @tparam PlatformImpl A user supplied platform specific features. See ExamplePlatformImpl.
 */
template<size_t maxLoggers, class PlatformImpl>
class LogServer : public LogServerBase {


public:

    /**
     * @brief Register a LocalLogger. To be called from LocalLogger thread only.
     * 
     * @tparam n 
     * @tparam sz 
     * @param logger 
     */
    template<size_t n, size_t sz>
    void addLogger(LocalLogger<n, sz> & logger){
        platform.lock();
        if(nLoggers == maxLoggers){
            QUICKLOG_ERROR("Attempt to add more than maxLoggers loggers to LogServer.\n");
        }else{
            localLoggers[nLoggers] =  static_cast<LocalLoggerBase*>( & logger);
            ++nLoggers;
        }
        logger.server = this;
        platform.unlock();
    }

    /**
     * @brief Cause the LogServer thread to finish printing any available log entries and exit.
     * 
     */
    void shutdown(){
        run = false;
        platform.notify();
    }

    /**
     * @brief main() function for LogServer thread.
     * 
     * @param arg pointer to a LogServer instance.
     */
    static void* process(void *arg){
        auto self = static_cast< LogServer* >(arg);
        self->_process();
        return nullptr;
    }

private:
    void _process(){
        while(run){
            platform.wait();
            _dumpAll();
        }
        _dumpAll();
    }

    void _dumpAll(){
        platform.lock();
        bool didSomething;
        do{
            didSomething = false;
            for(size_t i=0; i<nLoggers; i++){
                didSomething |= localLoggers[i]->dump();
            }
        }while(didSomething);
        platform.unlock();
    }


    void _onDumpAvail(){
        platform.notify();
    }

    std::array<LocalLoggerBase*, maxLoggers> localLoggers;
    size_t nLoggers = 0;
    volatile bool run = true;
    PlatformImpl platform;
};


} // namespace quicklog

