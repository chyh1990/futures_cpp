#pragma once

#include <limits>
#include <ostream>
#include <stdexcept>
#include <futures/Core.h>
#include <futures/Async.h>
#include <futures_mysql/SqlTypes.h>
#include <futures_mysql/Exception.h>
#include <futures_mysql/MySql.h>

namespace futures {
namespace mysql {

class Field {
public:
    explicit Field(MYSQL_FIELD *col) {
        catalog_.assign(col->catalog, col->catalog_length);
        db_.assign(col->db, col->db_length);
        table_.assign(col->table, col->table_length);
        orig_table_ = col->org_table;
        name_.assign(col->name, col->name_length);
        orig_name_.assign(col->org_name, col->org_name_length);
        type_ = col->type;
        charset_ = col->charsetnr;
    }

    void dump(std::ostream &os) const {
        os << "Field:    catalog=" << catalog_ << "\n";
        os << "              name=" << name_ << "\n";
        os << "              type=" << type_ << "\n";
        os << std::endl;
    }

    int getType() const {
        return type_;
    }
private:
    std::string catalog_;
    std::string db_;
    std::string table_;
    std::string orig_table_;
    std::string name_;
    std::string orig_name_;
    int charset_;
    size_t size_;
    size_t max_size_;
    int type_;
    int flags_;

};

class ResultSet;

using Fields = std::vector<Field>;
using FieldsPtr = std::shared_ptr<Fields>;

class Row {
public:
    Row(FieldsPtr fields, const MYSQL_ROW r)
        : fields_(fields) {
        for (size_t i = 0; i < fields->size(); ++i) {
            if (r[i]) {
                v_.push_back(Optional<std::string>(r[i]));
            } else {
                v_.push_back(folly::none);
            }
        }
    }

    void dump(std::ostream &os) const {
        os << "| ";
        for (auto &e : v_) {
            if (e) {
                os << *e << " | ";
            } else {
                os << "NULL | ";
            }
        }
        os << std::endl;
    }

    const Optional<std::string> &getField(size_t idx) const {
        return v_[idx];
    }

    CellDataType get(size_t idx) const {
        if (!v_[idx]) return NullType();
        if (!fields_) throw MySqlException("Field meta not available.");
        int type = fields_->at(idx).getType();
        long long v;
        float fv;
        double dv;
        switch (type) {
        case MYSQL_TYPE_NULL:
            return NullType();
        case MYSQL_TYPE_TINY:
            v = std::stoll(*v_[idx]);
            return CellDataType(checkedCast<TinyType>(v));
        case MYSQL_TYPE_SHORT:
            v = std::stoll(*v_[idx]);
            return CellDataType(checkedCast<ShortType>(v));
        case MYSQL_TYPE_LONG:
            v = std::stoll(*v_[idx]);
            return CellDataType(checkedCast<LongType>(v));
        case MYSQL_TYPE_LONGLONG:
            v = std::stoll(*v_[idx]);
            return CellDataType(checkedCast<LongLongType>(v));
        case MYSQL_TYPE_FLOAT:
            fv = std::stof(*v_[idx]);
            return CellDataType(FloatType{fv});
        case MYSQL_TYPE_DOUBLE:
            dv = std::stod(*v_[idx]);
            return CellDataType(DoubleType{dv});
        case MYSQL_TYPE_STRING:
            return CellDataType(StringType{*v_[idx]});
        default:
            throw MySqlException("Field type not supported: " + std::to_string(type));
        }
    }

private:
    FieldsPtr fields_;
    std::vector<Optional<std::string>> v_;

    template <typename T>
    static T checkedCast(long long v) {
        if (v < (long long)std::numeric_limits<T>::min())
            throw std::underflow_error("bad number");
        if (v > (long long)std::numeric_limits<T>::max())
            throw std::overflow_error("bad number");
        return (T)v;
    }
};

class ResultSet {
public:
    explicit ResultSet(MYSQL_RES* r)
        : fields_(std::make_shared<Fields>()) {
        set(r);
    }

    ResultSet()
        : fields_(std::make_shared<Fields>()) {}

    void set(MYSQL_RES* r) {
        row_count_ = mysql_num_rows(r);
        unsigned int fields = mysql_num_fields(r);
        for (unsigned int i = 0; i < fields; i++) {
            fields_->push_back(Field(mysql_fetch_field(r)));
        }
    }

    void addRow(MYSQL_ROW r) {
        rows_.push_back(Row(fields_, r));
    }

    void setAffectedRows(size_t r) {
        affected_rows_ = r;
    }

    size_t getAffectedRows() const {
        return affected_rows_;
    }

    void setInsertId(size_t r) {
        insert_id_ = r;
    }

    size_t getInsertId() const {
        return insert_id_;
    }

    const std::vector<Field> & getFields() const {
        return *fields_.get();
    }

    FieldsPtr getFieldsPtr() const {
        return fields_;
    }

    size_t getFieldCount() const {
        return getFields().size();
    }

    const std::vector<Row> &getBufferedRows() const {
        return rows_;
    }

    void dump(std::ostream &os) const {
        os << "ResultSet:    row_count=" << row_count_ << "\n";
        os << "          affected_rows=" << affected_rows_<< "\n";
        os << "              insert_id=" << insert_id_ << "\n";
        for (auto &e: *fields_)
            e.dump(os);
        os << std::endl;
        for (auto &e: rows_)
            e.dump(os);
    }

    void clear() {
        row_count_ = 0;
        affected_rows_ = 0;
        fields_->clear();
        rows_.clear();
    }

private:
    FieldsPtr fields_;
    std::vector<Row> rows_;
    size_t row_count_ = 0;
    size_t affected_rows_ = 0;
    size_t insert_id_ = 0;
};

}
}
