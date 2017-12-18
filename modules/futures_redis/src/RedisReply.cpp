#include <ostream>
#include "hiredis/hiredis.h"
#include <futures_redis/RedisReply.h>

namespace futures {
namespace redis_io {

Reply::Reply(redisReply *c_reply):
_type(Reply::type_t::ERROR),
_integer(0)
{
    _type = static_cast<type_t>(c_reply->type);
    switch (_type) {
	case type_t::ERROR:
	case type_t::STRING:
	case type_t::STATUS:
            _str.assign(c_reply->str, c_reply->len);
            break;
	case type_t::INTEGER:
            _integer = c_reply->integer;
            break;
	case type_t::ARRAY:
            for (size_t i=0; i < c_reply->elements; ++i) {
                _elements.push_back(Reply(c_reply->element[i]));
            }
            break;
        default:
            break;
    }
}

Reply::Reply():
_type(Reply::type_t::ERROR),
_integer(0) {
}

void Reply::_dump(std::ostream &os, int indent) {
    for (int i = 0; i < indent; ++i)
        os << " ";
    switch (type()) {
        case type_t::STRING:
            os << str();
            break;
        case type_t::INTEGER:
            os << integer();
            break;
        case type_t::NIL:
            os << "<NIL>";
            break;
        case type_t::STATUS:
            os << "<STATUS> '" << str() << "'";
            break;
        case type_t::ERROR:
            os << "<ERROR> '" << str() << "'";
            break;
        case type_t::ARRAY:
            for (size_t i = 0 ; i < _elements.size(); ++i) {
                os << '(' << i << ") ";
                _elements[i]._dump(os, indent + 2);
            }
            break;
        default:
            break;
    }
    os << "\n";
}

}
}
