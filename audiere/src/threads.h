/// Internal platform-specific threads

#ifndef THREADS_HPP
#define THREADS_HPP


#include "debug.h"


namespace audiere {

  typedef void (*AI_ThreadRoutine)(void* opaque);

  struct AI_CriticalSectionStruct;
  typedef AI_CriticalSectionStruct* AI_CriticalSection;


  // threads
  bool AI_CreateThread(AI_ThreadRoutine routine, void* opaque, int priority = 0);

  // waiting
  void AI_Sleep(unsigned milliseconds);

  // critical section
  AI_CriticalSection AI_CreateCriticalSection();
  void AI_DestroyCriticalSection(AI_CriticalSection cs);
  void AI_EnterCriticalSection(AI_CriticalSection cs);
  void AI_LeaveCriticalSection(AI_CriticalSection cs);


  class Synchronized {
  public:
    Synchronized() {
      m_cs = AI_CreateCriticalSection();
      // if it fails...  shit
    }

    ~Synchronized() {
      AI_DestroyCriticalSection(m_cs);
    }

    void lock() {
   
      ADR_LOG("--> Trying to lock");
      AI_EnterCriticalSection(m_cs);
      ADR_LOG("-->:: LOCKED ::");
    }

    void unlock() {
      ADR_LOG("<-- Unlocking...");
      AI_LeaveCriticalSection(m_cs);
    }

  private:
    AI_CriticalSection m_cs;
  };


  class Lock {
  public:
    Lock(Synchronized* object)
    : m_object(object) {
      object->lock();
    }

    ~Lock() {
      m_object->unlock();
    }

  private:
    Synchronized* m_object;
  };

}

#endif