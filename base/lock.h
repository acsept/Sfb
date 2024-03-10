
#ifndef __LOCK_H__
#define __LOCK_H__

#include "ostype.h"


class CLock {
  public:
    CLock();
    virtual ~CLock();
    void lock();
    void unlock();
    pthread_mutex_t &getMutex() { return lock_; }
#ifndef _WIN32
    virtual bool try_lock();
#endif
  private:
#ifdef _WIN32
    CRITICAL_SECTION m_critical_section;
#else
    pthread_mutex_t lock_;
#endif
};

#ifndef _WIN32
class CRWLock {
  public:
    CRWLock();
    virtual ~CRWLock();
    void rlock();
    void wlock();
    void unlock();
    bool try_rlock();
    bool try_wlock();

  private:
    pthread_rwlock_t lock_;
};

class CAutoRWLock {
  public:
    CAutoRWLock(CRWLock *pLock, bool bRLock = true);
    virtual ~CAutoRWLock();

  private:
    CRWLock *lock_;
};
#endif

class CAutoLock {
  public:
    CAutoLock(CLock *pLock);
    virtual ~CAutoLock();

  private:
    CLock *m_pLock;
};





#endif