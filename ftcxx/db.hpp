/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#pragma once

#include <string>

#include <db.h>

#include "cursor.hpp"
#include "db_env.hpp"
#include "db_txn.hpp"
#include "exceptions.hpp"
#include "slice.hpp"
#include "stats.hpp"

namespace ftcxx {

    template<class Comparator, class Handler>
    class CallbackCursor;
    template<class Comparator, class Predicate>
    class BufferedCursor;
    template<class Comparator>
    class SimpleCursor;

    class DB {
    public:
        DB()
            : _db(nullptr)
        {}

        explicit DB(::DB *d)
            : _db(d)
        {}

        ~DB() {
            if (_db) {
                close();
            }
        }

        DB(const DB &) = delete;
        DB& operator=(const DB &) = delete;

        DB(DB &&o)
            : _db(nullptr)
        {
            std::swap(_db, o._db);
        }

        DB& operator=(DB &&o) {
            std::swap(_db, o._db);
            return *this;
        }

        ::DB *db() const { return _db; }

        Slice descriptor() const {
            return Slice(_db->cmp_descriptor->dbt);
        }

        template<typename Callback>
        int getf_set(const DBTxn &txn, const Slice &key, int flags, Callback cb) const {
            class WrappedCallback {
                Callback &_cb;
            public:
                WrappedCallback(Callback &cb_)
                    : _cb(cb_)
                {}

                static int call(const DBT *key_, const DBT *val_, void *extra) {
                    WrappedCallback *wc = static_cast<WrappedCallback *>(extra);
                    return wc->call(key_, val_);
                }

                int call(const DBT *key_, const DBT *val_) {
                    return _cb(Slice(*key_), Slice(*val_));
                }
            } wc(cb);

            DBT kdbt = key.dbt();
            return _db->getf_set(_db, txn.txn(), flags, &kdbt, &WrappedCallback::call, &wc);
        }

        int put(const DBTxn &txn, DBT *key, DBT *val, int flags=0) const {
            return _db->put(_db, txn.txn(), key, val, flags);
        }

        int put(const DBTxn &txn, const Slice &key, const Slice &val, int flags=0) const {
            DBT kdbt = key.dbt();
            DBT vdbt = val.dbt();
            return put(txn, &kdbt, &vdbt, flags);
        }

        int del(const DBTxn &txn, DBT *key, int flags=0) const {
            return _db->del(_db, txn.txn(), key, flags);
        }

        int del(const DBTxn &txn, const Slice &key, int flags=0) const {
            DBT kdbt = key.dbt();
            return _db->del(_db, txn.txn(), &kdbt, flags);
        }

        Stats get_stats() const {
            Stats stats;
            DB_BTREE_STAT64 s = {0, 0, 0};
            int r = _db->stat64(_db, NULL, &s);
            handle_ft_retval(r);
            stats.dataSize = s.bt_dsize;
            stats.fileSize = s.bt_fsize;
            stats.numberOfKeys = s.bt_nkeys;
            return stats;
        }

        struct NullFilter {
            bool operator()(const Slice &, const Slice &) {
                return true;
            }
        };

        /**
         * Constructs a Cursor over this DB, over the range from left to
         * right (or right to left if !forward).
         */
        template<class Comparator, class Handler>
        CallbackCursor<Comparator, Handler> cursor(const DBTxn &txn, DBT *left, DBT *right,
                                                   Comparator &&cmp, Handler &&handler, int flags=0,
                                                   bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator, class Handler>
        CallbackCursor<Comparator, Handler> cursor(const DBTxn &txn, const Slice &left, const Slice &right,
                                                   Comparator &&cmp, Handler &&handler, int flags=0,
                                                   bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator, class Handler>
        CallbackCursor<Comparator, Handler> cursor(const DBTxn &txn, Comparator &&cmp, Handler &&handler,
                                                   int flags=0, bool forward=true, bool prelock=true) const;

        template<class Comparator, class Predicate>
        BufferedCursor<Comparator, Predicate> buffered_cursor(const DBTxn &txn, DBT *left, DBT *right,
                                                              Comparator &&cmp, Predicate &&filter, int flags=0,
                                                              bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator, class Predicate>
        BufferedCursor<Comparator, Predicate> buffered_cursor(const DBTxn &txn, const Slice &left, const Slice &right,
                                                              Comparator &&cmp, Predicate &&filter, int flags=0,
                                                              bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator, class Predicate>
        BufferedCursor<Comparator, Predicate> buffered_cursor(const DBTxn &txn, Comparator &&cmp, Predicate &&filter,
                                                              int flags=0, bool forward=true, bool prelock=true) const;

        template<class Comparator>
        SimpleCursor<Comparator> simple_cursor(const DBTxn &txn, DBT *left, DBT *right,
                                               Comparator &&cmp, Slice &key, Slice &val, int flags=0,
                                               bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator>
        SimpleCursor<Comparator> simple_cursor(const DBTxn &txn, const Slice &left, const Slice &right,
                                               Comparator &&cmp, Slice &key, Slice &val, int flags=0,
                                               bool forward=true, bool end_exclusive=false, bool prelock=true) const;

        template<class Comparator>
        SimpleCursor<Comparator> simple_cursor(const DBTxn &txn, Comparator &&cmp, Slice &key, Slice &val,
                                               int flags=0, bool forward=true, bool prelock=true) const;

        void close() {
            int r = _db->close(_db, 0);
            handle_ft_retval(r);
            _db = nullptr;
        }

    private:
        ::DB *_db;
    };

    class DBBuilder {
        uint32_t _readpagesize;
        TOKU_COMPRESSION_METHOD _compression_method;
        uint32_t _fanout;
        uint8_t _memcmp_magic;
        uint32_t _pagesize;
        Slice _descriptor;

    public:
        DBBuilder()
            : _readpagesize(0),
              _compression_method(TOKU_COMPRESSION_METHOD(0)),
              _fanout(0),
              _memcmp_magic(0),
              _pagesize(0),
              _descriptor()
        {}

        DB open(const DBEnv &env, const DBTxn &txn, const char *fname, const char *dbname, DBTYPE dbtype, uint32_t flags, int mode) const {
            ::DB *db;
            int r = db_create(&db, env.env(), 0);
            handle_ft_retval(r);

            if (_readpagesize) {
                r = db->set_readpagesize(db, _readpagesize);
                handle_ft_retval(r);
            }

            if (_compression_method) {
                r = db->set_compression_method(db, _compression_method);
                handle_ft_retval(r);
            }

            if (_fanout) {
                r = db->set_fanout(db, _fanout);
                handle_ft_retval(r);
            }

            if (_memcmp_magic) {
                r = db->set_memcmp_magic(db, _memcmp_magic);
                handle_ft_retval(r);
            }

            if (_pagesize) {
                r = db->set_pagesize(db, _pagesize);
                handle_ft_retval(r);
            }

            r = db->open(db, txn.txn(), fname, dbname, dbtype, flags, mode);
            handle_ft_retval(r);

            if (!_descriptor.empty()) {
                DBT desc = _descriptor.dbt();
                r = db->change_descriptor(db, txn.txn(), &desc, DB_UPDATE_CMP_DESCRIPTOR);
                handle_ft_retval(r);
            }

            return DB(db);
        }

        DBBuilder& set_readpagesize(uint32_t readpagesize) {
            _readpagesize = readpagesize;
            return *this;
        }

        DBBuilder& set_compression_method(TOKU_COMPRESSION_METHOD _compressionmethod) {
            _compression_method = _compressionmethod;
            return *this;
        }

        DBBuilder& set_fanout(uint32_t fanout) {
            _fanout = fanout;
            return *this;
        }

        DBBuilder& set_memcmp_magic(uint8_t _memcmpmagic) {
            _memcmp_magic = _memcmpmagic;
            return *this;
        }

        DBBuilder& set_pagesize(uint32_t pagesize) {
            _pagesize = pagesize;
            return *this;
        }

        DBBuilder& set_descriptor(const Slice &desc) {
            _descriptor = desc;
            return *this;
        }
    };

} // namespace ftcxx
