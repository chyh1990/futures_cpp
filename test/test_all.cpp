
#include <gtest/gtest.h>
#include <futures_cpp.h>

#include <futures/Core.h>
#include <futures/core/Either.h>

#include <futures/Future.h>
// #include <futures/Task.h>
#include <futures/EventExecutor.h>
#include <futures/CpuPoolExecutor.h>
#include <futures/TcpStream.h>
#include <futures/Timer.h>

using namespace futures;

class MoveOnlyType {
public:
	MoveOnlyType(int v = 42): v_(v) {}
	MoveOnlyType(const MoveOnlyType&) = delete;
	MoveOnlyType& operator=(const MoveOnlyType&) = delete;

	MoveOnlyType(MoveOnlyType&&) = default;
	MoveOnlyType& operator=(MoveOnlyType&&) = default;

	int GetV() const { return v_; }
private:
	int v_;
};

TEST(Future, Trait) {
	EXPECT_FALSE(std::is_copy_constructible<OkFuture<int>>::value);
	EXPECT_TRUE(std::is_move_constructible<OkFuture<int>>::value);
}

#if 0
TEST(Future, Empty) {
	auto f = makeEmpty<int>();
	auto p = f.poll();
	auto &v = p.value();
	EXPECT_EQ(v, Async<int>());
	EXPECT_TRUE(v.isNotReady());
	auto v1 = v.map([] (int v) { return std::to_string(v); });
}
#endif

TEST(Future, Err) {
	auto f = ErrFuture<int>(folly::make_exception_wrapper<std::runtime_error>("bad"));
	EXPECT_TRUE(f.poll().hasException());
}

TEST(Future, Ok) {
	auto f = makeOk(5);
	auto p = f.poll();
	EXPECT_EQ(p.value().value(), 5);
	EXPECT_ANY_THROW(f.poll());
}

TEST(Future, Move) {
	auto f = makeOk(MoveOnlyType(42));
	auto f1 = f.andThen([] (MoveOnlyType v) {
			EXPECT_EQ(v.GetV(), 42);
			return makeOk();
	});
}

TEST(Future, Shared) {
	auto f = makeOk(42).shared();
	auto f1 = f;
	auto f2 = f;

	bool b = std::is_copy_constructible<SharedFuture<int>>::value;
	EXPECT_TRUE(b);

	EXPECT_EQ(f1.poll().value(), Async<int>(42));
	EXPECT_EQ(f2.poll().value(), Async<int>(42));
}

TEST(Future, AndThen) {
	auto f = makeOk(5);
	auto f1 = f.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return makeOk(0);
	});
	auto f2 = f1.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return makeOk('a');
	});
	auto f3 = f2.poll();
	EXPECT_EQ(f3.value(), Async<char>('a'));
}

TEST(Future, Join) {
	auto f = makeOk(1).join(makeOk(std::string("3")))
		.andThen2([] (int a, std::string b) {
			return makeOk(std::to_string(a) + b);
		});
	auto r = f.poll();
	EXPECT_EQ(r.value(), Async<std::string>("13"));
}

TEST(Future, Select) {
	auto f1 = makeOk(1);
	auto f2 = makeOk(2);
	std::vector<OkFuture<int>> fs;
	fs.push_back(std::move(f1));
	fs.push_back(std::move(f2));
	auto f = makeSelect(fs.begin(), fs.end());

	f.wait();
}

#if 1

TEST(Executor, Cpu) {
	CpuPoolExecutor exec(4);
	auto f = exec.spawn_fn([&] () {
			std::cerr << "Start" << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			std::cerr << "End" << std::endl;
			return 1;
	});

	auto f1 = f.andThen([] (int v) {
			return makeOk(v + 1);
	});

	EXPECT_EQ(f1.value(), Async<int>(2));
}

TEST(Executor, CpuExcept) {
	CpuPoolExecutor exec(4);
	auto f = exec.spawn_fn([&] () {
			std::cerr << "Start" << std::endl;
			throw std::runtime_error("error");
			return folly::Unit();
	});

	EXPECT_TRUE(f.wait().hasException());
}


#if 0
TEST(Executor, Event) {
	EventExecutor ev;

	auto f = tcp::Stream::connect(&ev, "127.0.0.1", 8111)
	.andThen([&ev] (tcp::Socket s) {
		std::cerr << "connected" << std::endl;
		return tcp::Stream::recv(&ev, std::move(s), io::TransferExactly(32));
	}).andThen2([&ev] (tcp::Socket s, std::unique_ptr<folly::IOBuf> buf) {
		buf->reserve(0, 32);
		memcpy(buf->writableTail(), " WORLD\n", 7);
		buf->append(7);
		return tcp::Stream::send(&ev, std::move(s), std::move(buf));
	}).andThen2([] (tcp::Socket s, size_t size) {
		std::cerr << "sent " << size << std::endl;
		return makeOk();
	}).then([] (Try<folly::Unit> u) {
		if (u.hasException())
			std::cerr << u.exception().what() << std::endl;
		return makeOk();
	});

	ev.spawn(std::move(f));
	ev.run();
	std::cerr << "END" << std::endl;

}
#endif

TEST(Executor, Timer) {
	EventExecutor ev;
	auto f = TimerFuture(&ev, 1)
		.andThen([&ev] (std::error_code _ec) {
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
		.then([] (Try<std::error_code> v) {
			if (v.hasException())
				std::cerr << "ERROR" << std::endl;
			else
				std::cerr << "Timer1 done" << std::endl;
			return makeOk(1);
		}).boxed());
	f.emplace_back(TimerFuture(&ev, 2.0)
		.then([] (Try<std::error_code> v) {
			if (v.hasException())
				std::cerr << "ERROR" << std::endl;
			else
				std::cerr << "Timer2 done" << std::endl;
			return makeOk(2);
		}).boxed());
	auto all = whenAll(f.begin(), f.end())
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
			[&ev, &v, n] (std::error_code) {
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

TEST(Either, NotSame)
{
	folly::Either<int, double> e1(folly::left_tag, 1);
	folly::Either<std::string, double> e2(folly::left_tag, std::string("AAA"));
	EXPECT_EQ(e2.left(), "AAA");
	auto e3 = e2;
	EXPECT_EQ(e3, e2);
	EXPECT_EQ(e3.left(), "AAA");
	auto e4 = std::move(e3);
	EXPECT_EQ(e4.left(), "AAA");
	EXPECT_FALSE(e3.hasRight());
	EXPECT_FALSE(e3.hasLeft());
	EXPECT_TRUE(e4.hasLeft());

	e4.assignRight(5.0);
	EXPECT_TRUE(e4.hasRight());
	EXPECT_NE(e4, e2);

	auto e10 = folly::make_left<std::string, int>("XX");
	EXPECT_EQ(e10.left(), "XX");
	auto e11 = folly::make_right<int, std::string>("XX");
	EXPECT_EQ(e11.right(), "XX");

	auto e12 = folly::make_left<MoveOnlyType, std::string>(MoveOnlyType(4));
	EXPECT_EQ(e12.left().GetV(), 4);

}

TEST(Either, Same)
{
	folly::Either<int, int> e1(folly::left_tag, 1);
}


#endif

TEST(Functional, apply) {
	auto f1 = [] (int a) { return a + 1; };
	auto f2 = [] (int a, double b) { return a + b; };
	auto r1 = folly::apply(f1, 1);
	EXPECT_EQ(r1, 2);
	auto t = std::make_tuple(1, 1.0);
	auto r2 = folly::applyTuple(f2, t);
	EXPECT_EQ(r2, 2.0);

	auto f3 = [] (MoveOnlyType v, int k) { return MoveOnlyType(v.GetV() + k); };
	auto r3 = folly::apply(f3, MoveOnlyType(2), 1);
	EXPECT_EQ(r3.GetV(), 3);

	auto r3_1 = folly::applyTuple(f3, std::make_tuple(MoveOnlyType(2), 1));
	EXPECT_EQ(r3_1.GetV(), 3);
}


int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

