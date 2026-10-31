#pragma once

#include "ppsspp_config.h"
#if !PPSSPP_PLATFORM(WII)
#include "thread/threadpool.h"
#include <functional>
#include <utility>
#endif

class GlobalThreadPool {
public:
	// Run [lower, upper), using workers only where the platform benefits.
	template <typename F>
	static void Loop(F &&loop, int lower, int upper) {
#if PPSSPP_PLATFORM(WII)
		// Wii has one CPU core. Call directly: no worker, thread-pool
		// dependency, std::function wrapper, or scheduler bookkeeping.
		loop(lower, upper);
#else
		LoopFunction(std::function<void(int, int)>(std::forward<F>(loop)), lower, upper);
#endif
	}

private:
#if !PPSSPP_PLATFORM(WII)
	static std::shared_ptr<ThreadPool> pool;
	static bool initialized;
	static void LoopFunction(const std::function<void(int, int)> &loop, int lower, int upper);
	static void Initialize();
#endif
};
