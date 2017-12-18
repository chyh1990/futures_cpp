
#include <gtest/gtest.h>
#include <futures/Timer.h>
#include <futures_zookeeper/ZkClient.h>

using namespace futures;

static zookeeper::ZkClient::Ptr makeClient(EventExecutor &ev) {
	return std::make_shared<zookeeper::ZkClient>(&ev, "127.0.0.1:2181");
}

static std::string genTestPath() {
	return "/test_unit_" + std::to_string(time(0))
		+ "_" + std::to_string(rand());
}

TEST(ZkClient, GetChildren) {
	EventExecutor ev(true);
	auto zk = makeClient(ev);
	auto f = (zk->getChildren("/")
		| [] (zookeeper::StringList sl) {
			for (auto &e: sl)
				std::cout << e << std::endl;
			return unit;
		}).error([] (folly::exception_wrapper w) {
			FUTURES_LOG(ERROR) << w.what();
		});
	ev.spawn(std::move(f));
	ev.run();
}

TEST(ZkClient, CreateAndDelete)
{
	EventExecutor ev(true);
	auto zk = makeClient(ev);
	auto test_path = genTestPath();
	auto f = (zk->createNode(test_path, std::string("TEST"), nullptr, 0)
		>> [zk, test_path] (std::string path) {
			std::cout << path << std::endl;
			return zk->getData(test_path);
		}
		>> [zk, test_path] (std::string data) {
			EXPECT_EQ(data, "TEST");
			return zk->deleteNode(test_path, 0);
		}
		).error([] (folly::exception_wrapper w) {
			FUTURES_LOG(ERROR) << w.what();
		});
	ev.spawn(std::move(f));
	ev.run();
}

TEST(ZkClient, Watch) {
	using namespace zookeeper;
	auto test_path = genTestPath();
	EventExecutor ev(true);
	auto zk = makeClient(ev);

	auto watcher =
		zk->getChildrenW("/")
		.andThen2([zk] (StringList list, PromiseFuture<WatchedEvent> ev) {
			return ev.andThen([zk] (zookeeper::WatchedEvent ev) {
					FUTURES_LOG(INFO) << "EVENT: " << ev.type << ", " << ev.path;
					if (ev.type == zookeeper::EventType::Child) {
						return (zk->getChildren("/")
							>> [] (zookeeper::StringList list) {
								for (auto &e: list)
									std::cout << e << std::endl;
								EventExecutor::current()->stop();
								return makeOk();
							}).boxed();
					} else {
						return makeOk().boxed();
					}
				});
		})
		.error([] (folly::exception_wrapper w) {
			FUTURES_LOG(ERROR) << w.what();
		});
	auto creater = delay(&ev, 0.5)
		>> [zk, test_path] (Unit) {
			return zk->createNode(test_path, std::string("data"), nullptr, 0);
		}
		>> [] (std::string s) {
			FUTURES_LOG(INFO) << "created: " << s;
			return makeOk();
		};
	ev.spawn(std::move(creater));
	ev.spawn(std::move(watcher));
	ev.run();
}

int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	srand(time(0));
	return RUN_ALL_TESTS();
}

