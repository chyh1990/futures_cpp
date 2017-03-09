#include <gtest/gtest.h>
#include <futures/Timer.h>
#include <futures/io/AsyncFile.h>

using namespace futures;

TEST(File, Read) {
    EventExecutor ev;
    auto f = std::make_shared<io::AsyncFile>();
    f->openSync("./src/miniz/miniz.c", O_RDONLY);
    EXPECT_TRUE(f->isValid());

    auto d = (delay(&ev, 0.2)
        >> [f] (Unit) {
            return f->read(128);
        }
        >> [] (std::unique_ptr<folly::IOBuf> buf) {
            std::cout << "=========" << std::endl;
            std::cout << buf->coalesce().str() << std::endl;
            std::cout << "=========" << std::endl;
            return makeOk();
        }
        )
        .error([] (folly::exception_wrapper w) {
            FUTURES_LOG(ERROR) << w.what();
        });
    ev.spawn(std::move(d));
    ev.run();

    f->closeSync();
}

TEST(File, Error) {
    auto f = std::make_shared<io::AsyncFile>();
    EXPECT_THROW(f->openSync("/NOT_EXISTS/PATH/xxx", O_RDONLY), std::system_error);
}

