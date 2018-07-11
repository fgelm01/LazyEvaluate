#ifndef OQTON_LAZY_EVALUATE_THREAD_POOL_SINGLETON_H
#define OQTON_LAZY_EVALUATE_THREAD_POOL_SINGLETON_H

#include "ThreadPool.h"
#include <thread>

namespace libraries_oqton {
namespace LazyEvaluate {

class ThreadPoolSingleton {
public:
  static ThreadPool* Instance();

private:
  static std::atomic<ThreadPool*> pinstance;
  static std::mutex m_mutex;
};

inline std::atomic<ThreadPool*> ThreadPoolSingleton::pinstance;
inline std::mutex ThreadPoolSingleton::m_mutex;

inline ThreadPool* ThreadPoolSingleton::Instance() {
  if (pinstance == nullptr) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (pinstance == nullptr)
      pinstance = new ThreadPool(std::thread::hardware_concurrency());
  }

  return pinstance;
}

}
}

#endif
