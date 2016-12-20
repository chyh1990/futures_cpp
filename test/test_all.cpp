
#include <gtest/gtest.h>
#include <futures_cpp.h>

#include <futures/core/ExceptionWrapper.h>
#include <futures/core/Try.h>
#include <futures/core/Optional.h>

#include <futures/Future.h>
// #include <futures/Task.h>
#include <futures/CpuPoolExecutor.h>

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

TEST(Future, Empty) {
	auto f = makeEmpty<int>();
	auto p = f.poll();
	auto &v = p.value();
	EXPECT_EQ(v, Async<int>());
	EXPECT_TRUE(v.isNotReady());
	auto v1 = v.map([] (int v) { return std::to_string(v); });
}

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
#endif


int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

