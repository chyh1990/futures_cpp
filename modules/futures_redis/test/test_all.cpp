
#include <gtest/gtest.h>
#include <futures_redis/RedisFuture.h>
#include <futures/Timer.h>

using namespace futures;

#if 0
static BoxedFuture<folly::Unit> tryNtimes(redis_io::AsyncContextPtr redis, int n) {
	if (n <= 0)
		return makeOk().boxed();
	return redis_io::RedisCommandFuture(redis, "KEYS stat:failed:*")
		.then([redis, n] (Try<redis_io::Reply> e) {
				if (e.hasException())
					std::cerr << e.exception().what() << std::endl;
				else
					e->dump(std::cerr);
				return delay(redis->executor(), 1);
		}).then([redis, n] (Try<std::error_code> ec) {
			return tryNtimes(redis, n - 1);
		}).boxed();
}

TEST(Futures, Redis) {
	EventExecutor loop(true);
	auto redis = std::make_shared<redis_io::AsyncContext>(&loop, "127.0.0.1", 6379);
	loop.spawn(tryNtimes(redis, 3));
	loop.run();
}
#endif

int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

