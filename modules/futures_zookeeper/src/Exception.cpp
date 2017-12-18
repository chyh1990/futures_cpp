#include <futures_zookeeper/Exception.h>
#include <zookeeper.h>

namespace futures {
namespace zookeeper {

SystemErrorException::SystemErrorException(int code)
    : ZookeeperException(zerror(code), code) {}

ApiErrorException::ApiErrorException(int code)
    : ZookeeperException(zerror(code), code) {}

#define __IMPL_ZEX(n, m) \
n##Exception::n##Exception() \
    : ApiErrorException(m) {}

__IMPL_ZEX(NoNode, ZNONODE);
__IMPL_ZEX(NoAuth, ZNOAUTH);
__IMPL_ZEX(BadVersion, ZBADVERSION);
__IMPL_ZEX(NoChildrenForEphemerals, ZNOCHILDRENFOREPHEMERALS);
__IMPL_ZEX(NodeExists, ZNODEEXISTS);
__IMPL_ZEX(NotEmpty, ZNOTEMPTY);

}
}
