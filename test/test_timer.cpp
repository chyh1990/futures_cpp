#include <gtest/gtest.h>

#include <futures/EventExecutor.h>
#include <futures/Timeout.h>
#include <futures/Timer.h>
#include <futures/Future.h>

using namespace futures;

TEST(Executor, Timer) {
	EventExecutor ev;
	auto f = TimerFuture(&ev, 1)
		.andThen([&ev] (Unit) {
			std::cerr << "DONE" << std::endl;
			return makeOk();
		});

	ev.spawn(std::move(f));
	ev.run();
	std::cerr << "END" << std::endl;
}


TEST(Future, Timeout) {
	EventExecutor ev;

	auto f = makeEmpty<int>();

	auto f1 = timeout(&ev, std::move(f), 1.0)
		.then([] (Try<int> v) {
			if (v.hasException())
				std::cerr << "ERROR" << std::endl;
			return makeOk();
		});

	ev.spawn(std::move(f1));
	ev.run();
}

TEST(Future, AllTimeout) {
	EventExecutor ev;

	std::vector<BoxedFuture<int>> f;
	f.emplace_back(TimerFuture(&ev, 1.0)
		.then([] (Try<Unit> v) {
			if (v.hasException())
				std::cerr << "ERROR" << std::endl;
			else
				std::cerr << "Timer1 done" << std::endl;
			return makeOk(1);
		}).boxed());
	f.emplace_back(TimerFuture(&ev, 2.0)
		.then([] (Try<Unit> v) {
			if (v.hasException())
				std::cerr << "ERROR" << std::endl;
			else
				std::cerr << "Timer2 done" << std::endl;
			return makeOk(2);
		}).boxed());
	auto all = makeWhenAll(f.begin(), f.end())
		.andThen([] (std::vector<int> ids) {
		std::cerr << "done" << std::endl;
		return makeOk();
	});

	ev.spawn(std::move(all));
	ev.run();
}

BoxedFuture<std::vector<int>> rwait(EventExecutor &ev, std::vector<int> &v, int n) {
	if (n == 0)
		return makeOk(std::move(v)).boxed();
	return TimerFuture(&ev, 0.1)
		.andThen(
			[&ev, &v, n] (Unit) {
				v.push_back(n);
				return rwait(ev, v, n - 1);
			}).boxed();
}

TEST(Future, RecursiveTimer) {
	EventExecutor ev;
	std::vector<int> idxes;
	auto w10 = rwait(ev, idxes, 10)
		.andThen([] (std::vector<int> idxes) {
				for (auto e: idxes)
					std::cerr << e << std::endl;
				return makeOk();
		});
	ev.spawn(std::move(w10));
	ev.run();
}

TEST(Future, TimerKeeper) {
	EventExecutor ev;

	auto timer = std::make_shared<TimerKeeper>(&ev, 1);
	auto f = [timer, &ev] (double sec) {
		return delay(&ev, sec)
			.andThen([timer] (Unit) {
				return TimerKeeperFuture(timer);
			})
			.then([] (Try<Unit> err) {
				if (err.hasException()) {
					std::cerr << "ERR: " << err.exception().what() << std::endl;
				} else {
					std::cerr << "Timeout: " << (uint64_t)(EventExecutor::current()->getNow() * 1000.0) << std::endl;
				}
				return makeOk();
			});
	};
	ev.spawn(f(0.2));
	ev.spawn(f(0.4));
	ev.run();
}

