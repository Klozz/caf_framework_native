
#include <bpf_timeinstate.h>

#include <sys/sysinfo.h>

#include <numeric>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <android-base/unique_fd.h>
#include <bpf/BpfMap.h>
#include <cputimeinstate.h>
#include <libbpf.h>

namespace android {
namespace bpf {

static constexpr uint64_t NSEC_PER_SEC = 1000000000;
static constexpr uint64_t NSEC_PER_YEAR = NSEC_PER_SEC * 60 * 60 * 24 * 365;

using std::vector;

TEST(TimeInStateTest, SingleUidTimeInState) {
    auto times = getUidCpuFreqTimes(0);
    ASSERT_TRUE(times.has_value());
    EXPECT_FALSE(times->empty());
}

TEST(TimeInStateTest, SingleUidConcurrentTimes) {
    auto concurrentTimes = getUidConcurrentTimes(0);
    ASSERT_TRUE(concurrentTimes.has_value());
    ASSERT_FALSE(concurrentTimes->active.empty());
    ASSERT_FALSE(concurrentTimes->policy.empty());

    uint64_t policyEntries = 0;
    for (const auto &policyTimeVec : concurrentTimes->policy) policyEntries += policyTimeVec.size();
    ASSERT_EQ(concurrentTimes->active.size(), policyEntries);
}

static void TestConcurrentTimesConsistent(const struct concurrent_time_t &concurrentTime) {
    size_t maxPolicyCpus = 0;
    for (const auto &vec : concurrentTime.policy) {
        maxPolicyCpus = std::max(maxPolicyCpus, vec.size());
    }
    uint64_t policySum = 0;
    for (size_t i = 0; i < maxPolicyCpus; ++i) {
        for (const auto &vec : concurrentTime.policy) {
            if (i < vec.size()) policySum += vec[i];
        }
        ASSERT_LE(concurrentTime.active[i], policySum);
        policySum -= concurrentTime.active[i];
    }
    policySum = 0;
    for (size_t i = 0; i < concurrentTime.active.size(); ++i) {
        for (const auto &vec : concurrentTime.policy) {
            if (i < vec.size()) policySum += vec[vec.size() - 1 - i];
        }
        auto activeSum = concurrentTime.active[concurrentTime.active.size() - 1 - i];
        // This check is slightly flaky because we may read a map entry in the middle of an update
        // when active times have been updated but policy times have not. This happens infrequently
        // and can be distinguished from more serious bugs by re-running the test: if the underlying
        // data itself is inconsistent, the test will fail every time.
        ASSERT_LE(activeSum, policySum);
        policySum -= activeSum;
    }
}

static void TestUidTimesConsistent(const std::vector<std::vector<uint64_t>> &timeInState,
                                   const struct concurrent_time_t &concurrentTime) {
    ASSERT_NO_FATAL_FAILURE(TestConcurrentTimesConsistent(concurrentTime));
    ASSERT_EQ(timeInState.size(), concurrentTime.policy.size());
    uint64_t policySum = 0;
    for (uint32_t i = 0; i < timeInState.size(); ++i) {
        uint64_t tisSum =
                std::accumulate(timeInState[i].begin(), timeInState[i].end(), (uint64_t)0);
        uint64_t concurrentSum = std::accumulate(concurrentTime.policy[i].begin(),
                                                 concurrentTime.policy[i].end(), (uint64_t)0);
        if (tisSum < concurrentSum)
            ASSERT_LE(concurrentSum - tisSum, NSEC_PER_SEC);
        else
            ASSERT_LE(tisSum - concurrentSum, NSEC_PER_SEC);
        policySum += concurrentSum;
    }
    uint64_t activeSum = std::accumulate(concurrentTime.active.begin(), concurrentTime.active.end(),
                                         (uint64_t)0);
    EXPECT_EQ(activeSum, policySum);
}

TEST(TimeInStateTest, SingleUidTimesConsistent) {
    auto times = getUidCpuFreqTimes(0);
    ASSERT_TRUE(times.has_value());

    auto concurrentTimes = getUidConcurrentTimes(0);
    ASSERT_TRUE(concurrentTimes.has_value());

    ASSERT_NO_FATAL_FAILURE(TestUidTimesConsistent(*times, *concurrentTimes));
}

TEST(TimeInStateTest, AllUidTimeInState) {
    vector<size_t> sizes;
    auto map = getUidsCpuFreqTimes();
    ASSERT_TRUE(map.has_value());

    ASSERT_FALSE(map->empty());

    auto firstEntry = map->begin()->second;
    for (const auto &subEntry : firstEntry) sizes.emplace_back(subEntry.size());

    for (const auto &vec : *map) {
        ASSERT_EQ(vec.second.size(), sizes.size());
        for (size_t i = 0; i < vec.second.size(); ++i) ASSERT_EQ(vec.second[i].size(), sizes[i]);
    }
}

TEST(TimeInStateTest, SingleAndAllUidTimeInStateConsistent) {
    auto map = getUidsCpuFreqTimes();
    ASSERT_TRUE(map.has_value());
    ASSERT_FALSE(map->empty());

    for (const auto &kv : *map) {
        uint32_t uid = kv.first;
        auto times1 = kv.second;
        auto times2 = getUidCpuFreqTimes(uid);
        ASSERT_TRUE(times2.has_value());

        ASSERT_EQ(times1.size(), times2->size());
        for (uint32_t i = 0; i < times1.size(); ++i) {
            ASSERT_EQ(times1[i].size(), (*times2)[i].size());
            for (uint32_t j = 0; j < times1[i].size(); ++j) {
                ASSERT_LE((*times2)[i][j] - times1[i][j], NSEC_PER_SEC);
            }
        }
    }
}

TEST(TimeInStateTest, AllUidConcurrentTimes) {
    auto map = getUidsConcurrentTimes();
    ASSERT_TRUE(map.has_value());
    ASSERT_FALSE(map->empty());

    auto firstEntry = map->begin()->second;
    for (const auto &kv : *map) {
        ASSERT_EQ(kv.second.active.size(), firstEntry.active.size());
        ASSERT_EQ(kv.second.policy.size(), firstEntry.policy.size());
        for (size_t i = 0; i < kv.second.policy.size(); ++i) {
            ASSERT_EQ(kv.second.policy[i].size(), firstEntry.policy[i].size());
        }
    }
}

TEST(TimeInStateTest, SingleAndAllUidConcurrentTimesConsistent) {
    auto map = getUidsConcurrentTimes();
    ASSERT_TRUE(map.has_value());
    for (const auto &kv : *map) {
        uint32_t uid = kv.first;
        auto times1 = kv.second;
        auto times2 = getUidConcurrentTimes(uid);
        ASSERT_TRUE(times2.has_value());
        for (uint32_t i = 0; i < times1.active.size(); ++i) {
            ASSERT_LE(times2->active[i] - times1.active[i], NSEC_PER_SEC);
        }
        for (uint32_t i = 0; i < times1.policy.size(); ++i) {
            for (uint32_t j = 0; j < times1.policy[i].size(); ++j) {
                ASSERT_LE(times2->policy[i][j] - times1.policy[i][j], NSEC_PER_SEC);
            }
        }
    }
}

void TestCheckDelta(uint64_t before, uint64_t after) {
    // Times should never decrease
    ASSERT_LE(before, after);
    // UID can't have run for more than ~1s on each CPU
    ASSERT_LE(after - before, NSEC_PER_SEC * 2 * get_nprocs_conf());
}

TEST(TimeInStateTest, AllUidTimeInStateMonotonic) {
    auto map1 = getUidsCpuFreqTimes();
    ASSERT_TRUE(map1.has_value());
    sleep(1);
    auto map2 = getUidsCpuFreqTimes();
    ASSERT_TRUE(map2.has_value());

    for (const auto &kv : *map1) {
        uint32_t uid = kv.first;
        auto times = kv.second;
        ASSERT_NE(map2->find(uid), map2->end());
        for (uint32_t policy = 0; policy < times.size(); ++policy) {
            for (uint32_t freqIdx = 0; freqIdx < times[policy].size(); ++freqIdx) {
                auto before = times[policy][freqIdx];
                auto after = (*map2)[uid][policy][freqIdx];
                ASSERT_NO_FATAL_FAILURE(TestCheckDelta(before, after));
            }
        }
    }
}

TEST(TimeInStateTest, AllUidConcurrentTimesMonotonic) {
    auto map1 = getUidsConcurrentTimes();
    ASSERT_TRUE(map1.has_value());
    ASSERT_FALSE(map1->empty());
    sleep(1);
    auto map2 = getUidsConcurrentTimes();
    ASSERT_TRUE(map2.has_value());
    ASSERT_FALSE(map2->empty());

    for (const auto &kv : *map1) {
        uint32_t uid = kv.first;
        auto times = kv.second;
        ASSERT_NE(map2->find(uid), map2->end());
        for (uint32_t i = 0; i < times.active.size(); ++i) {
            auto before = times.active[i];
            auto after = (*map2)[uid].active[i];
            ASSERT_NO_FATAL_FAILURE(TestCheckDelta(before, after));
        }
        for (uint32_t policy = 0; policy < times.policy.size(); ++policy) {
            for (uint32_t idx = 0; idx < times.policy[policy].size(); ++idx) {
                auto before = times.policy[policy][idx];
                auto after = (*map2)[uid].policy[policy][idx];
                ASSERT_NO_FATAL_FAILURE(TestCheckDelta(before, after));
            }
        }
    }
}

TEST(TimeInStateTest, AllUidTimeInStateSanityCheck) {
    auto map = getUidsCpuFreqTimes();
    ASSERT_TRUE(map.has_value());

    bool foundLargeValue = false;
    for (const auto &kv : *map) {
        for (const auto &timeVec : kv.second) {
            for (const auto &time : timeVec) {
                ASSERT_LE(time, NSEC_PER_YEAR);
                if (time > UINT32_MAX) foundLargeValue = true;
            }
        }
    }
    // UINT32_MAX nanoseconds is less than 5 seconds, so if every part of our pipeline is using
    // uint64_t as expected, we should have some times higher than that.
    ASSERT_TRUE(foundLargeValue);
}

TEST(TimeInStateTest, AllUidConcurrentTimesSanityCheck) {
    auto concurrentMap = getUidsConcurrentTimes();
    ASSERT_TRUE(concurrentMap);

    bool activeFoundLargeValue = false;
    bool policyFoundLargeValue = false;
    for (const auto &kv : *concurrentMap) {
        for (const auto &time : kv.second.active) {
            ASSERT_LE(time, NSEC_PER_YEAR);
            if (time > UINT32_MAX) activeFoundLargeValue = true;
        }
        for (const auto &policyTimeVec : kv.second.policy) {
            for (const auto &time : policyTimeVec) {
                ASSERT_LE(time, NSEC_PER_YEAR);
                if (time > UINT32_MAX) policyFoundLargeValue = true;
            }
        }
    }
    // UINT32_MAX nanoseconds is less than 5 seconds, so if every part of our pipeline is using
    // uint64_t as expected, we should have some times higher than that.
    ASSERT_TRUE(activeFoundLargeValue);
    ASSERT_TRUE(policyFoundLargeValue);
}

TEST(TimeInStateTest, AllUidTimesConsistent) {
    auto tisMap = getUidsCpuFreqTimes();
    ASSERT_TRUE(tisMap.has_value());

    auto concurrentMap = getUidsConcurrentTimes();
    ASSERT_TRUE(concurrentMap.has_value());

    ASSERT_EQ(tisMap->size(), concurrentMap->size());
    for (const auto &kv : *tisMap) {
        uint32_t uid = kv.first;
        auto times = kv.second;
        ASSERT_NE(concurrentMap->find(uid), concurrentMap->end());

        auto concurrentTimes = (*concurrentMap)[uid];
        ASSERT_NO_FATAL_FAILURE(TestUidTimesConsistent(times, concurrentTimes));
    }
}

TEST(TimeInStateTest, RemoveUid) {
    uint32_t uid = 0;
    {
        // Find an unused UID
        auto times = getUidsCpuFreqTimes();
        ASSERT_TRUE(times.has_value());
        ASSERT_FALSE(times->empty());
        for (const auto &kv : *times) uid = std::max(uid, kv.first);
        ++uid;
    }
    {
        // Add a map entry for our fake UID by copying a real map entry
        android::base::unique_fd fd{
                bpf_obj_get(BPF_FS_PATH "map_time_in_state_uid_time_in_state_map")};
        ASSERT_GE(fd, 0);
        time_key_t k;
        ASSERT_FALSE(getFirstMapKey(fd, &k));
        std::vector<tis_val_t> vals(get_nprocs_conf());
        ASSERT_FALSE(findMapEntry(fd, &k, vals.data()));
        uint32_t copiedUid = k.uid;
        k.uid = uid;
        ASSERT_FALSE(writeToMapEntry(fd, &k, vals.data(), BPF_NOEXIST));

        android::base::unique_fd fd2{
                bpf_obj_get(BPF_FS_PATH "map_time_in_state_uid_concurrent_times_map")};
        k.uid = copiedUid;
        k.bucket = 0;
        std::vector<concurrent_val_t> cvals(get_nprocs_conf());
        ASSERT_FALSE(findMapEntry(fd2, &k, cvals.data()));
        k.uid = uid;
        ASSERT_FALSE(writeToMapEntry(fd2, &k, cvals.data(), BPF_NOEXIST));
    }
    auto times = getUidCpuFreqTimes(uid);
    ASSERT_TRUE(times.has_value());
    ASSERT_FALSE(times->empty());

    auto concurrentTimes = getUidConcurrentTimes(0);
    ASSERT_TRUE(concurrentTimes.has_value());
    ASSERT_FALSE(concurrentTimes->active.empty());
    ASSERT_FALSE(concurrentTimes->policy.empty());

    uint64_t sum = 0;
    for (size_t i = 0; i < times->size(); ++i) {
        for (auto x : (*times)[i]) sum += x;
    }
    ASSERT_GT(sum, (uint64_t)0);

    uint64_t activeSum = 0;
    for (size_t i = 0; i < concurrentTimes->active.size(); ++i) {
        activeSum += concurrentTimes->active[i];
    }
    ASSERT_GT(activeSum, (uint64_t)0);

    ASSERT_TRUE(clearUidTimes(uid));

    auto allTimes = getUidsCpuFreqTimes();
    ASSERT_TRUE(allTimes.has_value());
    ASSERT_FALSE(allTimes->empty());
    ASSERT_EQ(allTimes->find(uid), allTimes->end());

    auto allConcurrentTimes = getUidsConcurrentTimes();
    ASSERT_TRUE(allConcurrentTimes.has_value());
    ASSERT_FALSE(allConcurrentTimes->empty());
    ASSERT_EQ(allConcurrentTimes->find(uid), allConcurrentTimes->end());
}

} // namespace bpf
} // namespace android
