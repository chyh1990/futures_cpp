#include <gtest/gtest.h>

#include <futures/Core.h>
#include <futures/core/Either.h>

#include <futures/Future.h>
#include <futures/Promise.h>
// #include <futures/Task.h>
#include <futures/EventExecutor.h>
#include <futures/CpuPoolExecutor.h>
#include "HelperTypes.h"

using namespace futures;
using test::MoveOnlyType;

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
			return Unit();
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
	}).then([] (Try<Unit> u) {
		if (u.hasException())
			std::cerr << u.exception().what() << std::endl;
		return makeOk();
	});

	ev.spawn(std::move(f));
	ev.run();
	std::cerr << "END" << std::endl;

}
#endif

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

TEST(Channel, MPSC) {
	auto t = channel::makeUnboundedMPSCChannel<int>();
	auto s1 = t.first;
	auto s2 = t.first;
	s1.send(1);
	s1.send(2);
	EXPECT_EQ(t.second.poll()->value(), 1);
}

TEST(Promise, Simple) {
	Promise<int> p;
	auto f = p.getFuture();
#if __cplusplus >= 201402L
	std::thread t([p{std::move(p)}] () {
		p.setValue(3);
	});
#else
	auto m = folly::makeMoveWrapper(std::move(p));
	std::thread t([m] () {
		auto p = m.move();
		p.setValue(3);
	});
#endif

	EXPECT_EQ(f.value(), 3);
	t.join();

	makePromiseFuture(Try<int>(3));
	makeReadyPromiseFuture(3);
}

int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

