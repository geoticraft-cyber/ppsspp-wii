#include "ppsspp_config.h"
#include "ThreadPools.h"

#include "../Core/Config.h"

#if !PPSSPP_PLATFORM(WII)
std::shared_ptr<ThreadPool> GlobalThreadPool::pool;
bool  GlobalThreadPool::initialized = false;

void GlobalThreadPool::LoopFunction(const std::function<void(int, int)> &loop, int lower, int upper) {
	Initialize();
	pool->ParallelLoop(loop, lower, upper);
}

void GlobalThreadPool::Initialize() {
	if(!initialized) {
		pool = std::make_shared<ThreadPool>(g_Config.iNumWorkerThreads);
		initialized = true;
	}
}
#endif
