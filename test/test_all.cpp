
#include <gtest/gtest.h>
#include <futures_cpp.h>

#include <futures/core/ExceptionWrapper.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>

#include <futures/Future.h>
#include <futures/Task.h>
#include <futures/CpuPoolExecutor.h>

using namespace futures;

TEST(Future, Trait) {
	EXPECT_TRUE(std::is_copy_constructible<Future<int>>::value);
	EXPECT_TRUE(std::is_move_constructible<Future<int>>::value);
}

TEST(Future, Empty) {
	auto f = Future<int>::empty();
	auto p = f.poll();
	auto &v = p.value();
	EXPECT_EQ(v, Async<int>());
	EXPECT_TRUE(v.isNotReady());
	auto v1 = v.map([] (int v) { return std::to_string(v); });
}

TEST(Future, Err) {
	auto f = Future<int>::err(folly::make_exception_wrapper<std::runtime_error>("bad"));
	EXPECT_TRUE(f.poll().hasException());
}

TEST(Future, Ok) {
	auto f = Future<int>::ok(5);
	auto p = f.poll();
	EXPECT_EQ(p.value().value(), 5);
	EXPECT_ANY_THROW(f.poll());
}

TEST(Future, Chain) {
	auto f = Future<int>::ok(5);
	auto chain = makeChain(f, [] (Try<int> v) {
			return Future<int>::ok('c');
		}
	);
	auto t = chain.poll();
}

TEST(Future, Chain1) {
	auto f = Future<std::unique_ptr<int>>::ok(folly::make_unique<int>(5));
	auto chain = makeChain(f, [] (Try<std::unique_ptr<int>> v) {
			return Future<int>::ok('c');
		}
	);
	auto t = chain.poll();
}

TEST(Future, AndThen) {
	auto f = Future<int>::ok(5);
	auto f1 = f.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return Future<int>::ok(0);
	});
	auto f2 = f1.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return Future<char>::ok('a');
	});
	auto f3 = f2.poll();
	EXPECT_EQ(f3.value(), Async<char>('a'));
}

TEST(Executor, Cpu) {
	CpuPoolExecutor exec(4);
	Future<int> f = exec.spawn([&] () {
			std::cerr << "Start" << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			std::cerr << "End" << std::endl;
			return 1;
	});

	Future<int> f1 = f.andThen([] (int v) {
			return Future<int>::ok(v + 1);
	});

	EXPECT_EQ(f1.value(), Async<int>(2));
}

int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

