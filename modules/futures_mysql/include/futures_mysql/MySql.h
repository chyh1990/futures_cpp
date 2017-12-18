#pragma once

#include <memory>
#define define_bool 1
// #include <ma_global.h>
#include <mysql.h>

namespace futures {
namespace mysql {

class InitThread {
public:
    InitThread() {
        mysql_thread_init();
    }

    ~InitThread() {
        mysql_thread_end();
    }
};

class InitOnce {
public:
    static void init() {
        static InitOnce instance;
        thread_local InitThread thread_init;
    }
private:
    InitOnce() {
        if (mysql_library_init(0, NULL, NULL)) {
            fprintf(stderr, "Failed to init mysql\n");
            abort();
        }
    }

    ~InitOnce() {
        mysql_library_end();
    }

};


}
}
