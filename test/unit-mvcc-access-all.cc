#include "xxhash.h"
#include "Sto.hh"
#include "DB_params.hh"
#include "DB_index.hh"

using bench::split_version_helpers;
using bench::mvcc_ordered_index;
using bench::mvcc_unordered_index;
using bench::RowAccess;
using bench::access_t;

struct index_key {
    index_key() = default;
    index_key(int32_t k1, int32_t k2)
        : key_1(bench::bswap(k1)), key_2(bench::bswap(k2)) {}
    bool operator==(const index_key& other) const {
        return (key_1 == other.key_1) && (key_2 == other.key_2);
    }
    bool operator!=(const index_key& other) const {
        return !(*this == other);
    }

    int32_t key_1;
    int32_t key_2;
};

struct index_value {
    enum class NamedColumn : int {
        value_1 = 0,
        value_2a,
        value_2b
    };
    int64_t value_1;
    int64_t value_2a;
    int64_t value_2b;
};

struct index_value_part1 {
    int64_t value_1;
};

struct index_value_part2 {
    int64_t value_2a;
    int64_t value_2b;
};

namespace std {
template <>
struct hash<index_key> {
    static constexpr size_t xxh_seed = 0xdeadbeefdeadbeef;
    size_t operator()(const index_key& arg) const {
        return XXH64(&arg, sizeof(index_key), xxh_seed);
    }
};

inline ostream& operator<<(ostream& os, const index_key&) {
    os << "index_key";
    return os;
}
}

template<>
struct bench::SplitParams<index_value> {
    using split_type_list = std::tuple<index_value_part1, index_value_part2>;
    static constexpr auto split_builder = std::make_tuple(
        [](const index_value &in) -> index_value_part1 {
          index_value_part1 p1;
          p1.value_1 = in.value_1;
          return p1;
        },
        [](const index_value &in) -> index_value_part2 {
          index_value_part2 p2;
          p2.value_2a = in.value_2a;
          p2.value_2b = in.value_2b;
          return p2;
        }
    );
    static constexpr auto split_merger = std::make_tuple(
        [](index_value *out, const index_value_part1 *in1) -> void {
          out->value_1 = in1->value_1;
        },
        [](index_value *out, const index_value_part2 *in2) -> void {
          out->value_2a = in2->value_2a;
          out->value_2b = in2->value_2b;
        }
    );
    static constexpr auto map = [](int col_n) -> int {
        if (col_n == 0)
            return 0;
        return 1;
    };

    using layout_type = typename SplitMvObjectBuilder<split_type_list>::type;
    static constexpr size_t num_splits = std::tuple_size<split_type_list>::value;
};

namespace bench {

// Record accessor interface
template<typename A>
class RecordAccessor<A, index_value> {
 public:
    int64_t value_1() const {
        return impl().value_1_impl();
    }

    int64_t value_2a() const {
        return impl().value_2a_impl();

    }

    int64_t value_2b() const {
        return impl().value_2b_impl();
    }

 private:
    const A &impl() const {
        return *static_cast<const A *>(this);
    }
};

// Used by 1V split/non-split versions and MV non-split versions.
template<>
class UniRecordAccessor<index_value>
      : public RecordAccessor<UniRecordAccessor<index_value>, index_value> {
 public:
    UniRecordAccessor(const index_value *const vptr) : vptr_(vptr) {}

 private:
    int64_t value_1_impl() const {
        return vptr_->value_1;
    }

    int64_t value_2a_impl() const {
        return vptr_->value_2a;
    }

    int64_t value_2b_impl() const {
        return vptr_->value_2b;
    }

    const index_value *vptr_;

    friend RecordAccessor<UniRecordAccessor<index_value>, index_value>;
};

// Used by MVCC split version only.
template<>
class SplitRecordAccessor<index_value>
      : public RecordAccessor<SplitRecordAccessor<index_value>, index_value> {
 public:
    static constexpr size_t
        num_splits = bench::SplitParams<index_value>::num_splits;

    SplitRecordAccessor(const std::array<void *, num_splits> &vptrs)
        : vptr_1_(reinterpret_cast<index_value_part1 *>(vptrs[0])),
          vptr_2_(reinterpret_cast<index_value_part2 *>(vptrs[1])) {}

 private:
    int64_t value_1_impl() const {
        return vptr_1_->value_1;
    }

    int64_t value_2a_impl() const {
        return vptr_2_->value_2a;
    }

    int64_t value_2b_impl() const {
        return vptr_2_->value_2b;
    }

    const index_value_part1 *vptr_1_;
    const index_value_part2 *vptr_2_;

    friend RecordAccessor<SplitRecordAccessor<index_value>, index_value>;
};
}  // namespace bench

namespace commutators {
template <>
class Commutator<index_value> {
public:
    Commutator() = default;

    explicit Commutator(int64_t delta_value_1)
        : delta_value_1(delta_value_1), delta_value_2a(0), delta_value_2b(0) {}
    explicit Commutator(int64_t delta_value_2a, int64_t delta_value_2b)
        : delta_value_1(0), delta_value_2a(delta_value_2a),
          delta_value_2b(delta_value_2b) {}

    index_value& operate(index_value &value) const {
        value.value_1 += delta_value_1;
        value.value_2a += delta_value_2a;
        value.value_2b += delta_value_2b;
        return value;
    }

protected:
    int64_t delta_value_1;
    int64_t delta_value_2a;
    int64_t delta_value_2b;
    friend Commutator<index_value_part1>;
    friend Commutator<index_value_part2>;
};

template <>
class Commutator<index_value_part1> : Commutator<index_value> {
public:
    template <typename... Args>
    Commutator(Args&&... args) : Commutator<index_value>(std::forward<Args>(args)...) {}

    index_value_part1& operate(index_value_part1 &value) const {
        value.value_1 += delta_value_1;
        return value;
    }
};

template <>
class Commutator<index_value_part2> : Commutator<index_value> {
public:
    template <typename... Args>
    Commutator(Args&&... args) : Commutator<index_value>(std::forward<Args>(args)...) {}

    index_value_part2& operate(index_value_part2 &value) const {
        value.value_2a += delta_value_2a;
        value.value_2b += delta_value_2b;
        return value;
    }
};
}  // namespace commutators

template <bool Ordered>
class MVCCIndexTester {
public:
    void RunTests();

private:
    static constexpr size_t index_init_size = 1025;
    using key_type = typename std::conditional<Ordered, bench::masstree_key_adapter<index_key>, index_key>::type;
    using index_type = typename std::conditional<Ordered, mvcc_ordered_index<key_type, index_value, db_params::db_mvcc_params>,
                                                          mvcc_unordered_index<key_type, index_value, db_params::db_mvcc_params>>::type;
    typedef index_value::NamedColumn nc;

    void SelectSplitTest();
    void DeleteTest();
    void CommuteTest();
    void ScanTest();
    void InsertTest();
    void UpdateTest();
};

template <bool Ordered>
void MVCCIndexTester<Ordered>::RunTests() {
    if constexpr (Ordered) {
        printf("Testing Ordered Index (MVCC TS):\n");
    } else {
        printf("Testing Unordered Index (MVCC TS):\n");
    }
    SelectSplitTest();
    DeleteTest();
    CommuteTest();
    InsertTest();
    UpdateTest();
    if constexpr (Ordered) {
        ScanTest();
    }
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::SelectSplitTest() {
    index_type idx(index_init_size);
    idx.thread_init();

    key_type key{0, 1};
    index_value val{4, 5, 6};
    idx.nontrans_put(key, val);

    TestTransaction t(0);
    auto[success, result, row, accessor]
        = idx.select_split_row(key,
                               {{nc::value_1,access_t::read},
                                {nc::value_2b,access_t::read}});
    (void)row;
    assert(success);
    assert(result);

    assert(accessor.value_1() == 4);
    assert(accessor.value_2b() == 6);

    assert(t.try_commit());

    printf("Test pass: SelectSplitTest\n");
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::DeleteTest() {
    index_type idx(index_init_size);
    idx.thread_init();

    key_type key{0, 1};
    index_value val{4, 5, 6};
    idx.nontrans_put(key, val);

    {
        TestTransaction t1(0);
        TestTransaction t2(1);

        {
            t1.use();

            auto[success, found] = idx.delete_row(key);

            assert(success);
            assert(found);
        }

        // Concurrent observation should not observe delete
        {
            t2.use();
            auto[success, result, row, accessor]
                = idx.select_split_row(key,
                                       {{nc::value_1,access_t::read},
                                        {nc::value_2b,access_t::read}});
            (void)row; (void)accessor;
            assert(success);
            assert(result);
        }

        assert(t1.try_commit());

        assert(t2.try_commit());
    }

    // Serialized observation should observe delete
    {
        TestTransaction t(0);
        auto[success, result, row, accessor]
            = idx.select_split_row(key,
                                   {{nc::value_1,access_t::read},
                                    {nc::value_2b,access_t::read}});
        (void)row; (void)accessor;
        assert(success);
        assert(!result);

        assert(t.try_commit());
    }

    printf("Test pass: %s\n", __FUNCTION__);
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::CommuteTest() {
    using key_type = bench::masstree_key_adapter<index_key>;
    using index_type = mvcc_ordered_index<key_type,
                                          index_value,
                                          db_params::db_mvcc_params>;
    typedef index_value::NamedColumn nc;
    index_type idx(index_init_size);
    idx.thread_init();

    key_type key{0, 1};
    index_value val{4, 5, 6};
    idx.nontrans_put(key, val);

    for (int64_t i = 0; i < 10; i++) {
        TestTransaction t(0);
        auto[success, result, row, accessor]
            = idx.select_split_row(key,
                                   {{nc::value_1,access_t::write},
                                    {nc::value_2b,access_t::none}});
        (void)row; (void)accessor;
        assert(success);
        assert(result);

        {
            commutators::Commutator<index_value> comm(10 + i);
            idx.update_row(row, comm);
        }

        assert(t.try_commit());
    }

    {
        TestTransaction t(0);
        auto[success, result, row, accessor]
            = idx.select_split_row(key,
                                   {{nc::value_1,access_t::read},
                                    {nc::value_2b,access_t::read}});
        (void)row;
        assert(success);
        assert(result);

        assert(accessor.value_1() == 149);
        assert(accessor.value_2a() == 5);
        assert(accessor.value_2b() == 6);
    }


    printf("Test pass: %s\n", __FUNCTION__);
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::ScanTest() {
    if constexpr (!Ordered) {
        printf("Skipped: ScanTest\n");
    } else {
        using accessor_t = bench::SplitRecordAccessor<index_value>;
        using split_value_t = std::array<void*, bench::SplitParams<index_value>::num_splits>;
        index_type idx(index_init_size);
        idx.thread_init();

        {
            key_type key{0, 1};
            index_value val{4, 5, 6};
            idx.nontrans_put(key, val);
        }
        {
            key_type key{0, 2};
            index_value val{7, 8, 9};
            idx.nontrans_put(key, val);
        }
        {
            key_type key{0, 3};
            index_value val{10, 11, 12};
            idx.nontrans_put(key, val);
        }

        {
            TestTransaction t(0);

            auto scan_callback = [&] (const key_type& key, const split_value_t& split_values) -> bool {
                accessor_t accessor(split_values);
                std::cout << "Visiting key: {" << key.key_1 << ", " << key.key_2 << "}, value parts:" << std::endl;
                std::cout << "    " << accessor.value_1() << std::endl;
                std::cout << "    " << accessor.value_2a() << std::endl;
                std::cout << "    " << accessor.value_2b() << std::endl;
                return true;
            };

            key_type k0{0, 0};
            key_type k1{1, 0};

            bool success = idx.template range_scan<decltype(scan_callback), false>(k0, k1,
                                                                                   scan_callback,
                                                                                   {{nc::value_1,access_t::read},
                                                                                   {nc::value_2b,access_t::read}}, true);
            assert(success);
            assert(t.try_commit());
        }

        printf("Test pass: ScanTest\n");
    }
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::InsertTest() {
    index_type idx(index_init_size);
    idx.thread_init();

    {
        TestTransaction t(0);
        key_type key{0, 1};
        index_value val{4, 5, 6};

        auto[success, found] = idx.insert_row(key, &val);
        assert(success);
        assert(!found);
        assert(t.try_commit());
    }

    {
        TestTransaction t(1);
        key_type key{0, 1};
        auto[success, result, row, accessor] = idx.select_split_row(key, {{nc::value_1, access_t::read},
                                                                          {nc::value_2a, access_t::read},
                                                                          {nc::value_2b, access_t::read}});
        (void)row;
        assert(success);
        assert(result);

        assert(accessor.value_1() == 4);
        assert(accessor.value_2a() == 5);
        assert(accessor.value_2b() == 6);
        assert(t.try_commit());
    }

    printf("Test pass: InsertTest\n");
}

template <bool Ordered>
void MVCCIndexTester<Ordered>::UpdateTest() {
    index_type idx(index_init_size);
    idx.thread_init();

    {
        TestTransaction t(0);
        key_type key{0, 1};
        index_value val{4, 5, 6};

        auto[success, found] = idx.insert_row(key, &val);
        assert(success);
        assert(!found);
        assert(t.try_commit());
    }

    {
        TestTransaction t(1);
        key_type key{0, 1};
        index_value new_val{7, 0, 0};
        auto[success, result, row, accessor] = idx.select_split_row(key, {{nc::value_1, access_t::update},
                                                                          {nc::value_2a, access_t::read},
                                                                          {nc::value_2b, access_t::read}});
        assert(success);
        assert(result);
        assert(accessor.value_1() == 4);
        assert(accessor.value_2a() == 5);
        assert(accessor.value_2b() == 6);

        idx.update_row(row, &new_val);

        assert(t.try_commit());
    }

    {
        TestTransaction t(0);
        key_type key{0, 1};
        auto[success, result, row, accessor] = idx.select_split_row(key,
            {{nc::value_1, access_t::read},
             {nc::value_2a, access_t::read},
             {nc::value_2b, access_t::read}});

        (void)row;
        assert(success);
        assert(result);
        assert(accessor.value_1() == 7);
        assert(accessor.value_2a() == 5);
        assert(accessor.value_2b() == 6);

        assert(t.try_commit());
    }

    printf("Test pass: UpdateTest\n");
}

int main() {
    {
        MVCCIndexTester<true> tester;
        tester.RunTests();
    }
    {
        MVCCIndexTester<false> tester;
        tester.RunTests();
    }
    printf("ALL TESTS PASS, ignore errors after this.\n");
    return 0;
}
