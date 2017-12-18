#include <iostream>
#include <futures_redis/RedisFuture.h>
#include <futures/io/Signal.h>
#include <futures/Timer.h>

using namespace futures;

int main(int argc, char *argv[]) {
    EventExecutor loop(true);
    auto redis = std::make_shared<redis_io::AsyncContext>(&loop, "127.0.0.1", 6379);

#if 0
    auto pull = timeout(&loop, redis_io::RedisCommandFuture(redis, "BLPOP testxxx 0"), 1.0)
	.then([] (Try<redis_io::Reply> reply) {
	if (reply.hasException())
	    std::cerr << "ERR: " << reply.exception().what() << std::endl;
	else
	    reply->dump(std::cerr);
	return makeOk();
    });
    loop.spawn(std::move(pull));
#else
    auto sub = redis->subscribe("SUBSCRIBE test_ch1")
	.forEach([] (redis_io::Reply reply) {
		// std::cerr << "B: ";
		reply.dump(std::cerr);
	})
	.then([] (Try<folly::Unit> err) {
		if (err.hasException())
		    std::cerr << err.exception().what() << std::endl;
		std::cerr << "END" << std::endl;
		return makeOk();
	});
    loop.spawn(std::move(sub));
#endif

    loop.spawn(io::signal(&loop, SIGINT).andThen([] (int num) {
		EventExecutor::current()->stop();
		return makeOk(); }
		));
    loop.run();

    return 0;
}
