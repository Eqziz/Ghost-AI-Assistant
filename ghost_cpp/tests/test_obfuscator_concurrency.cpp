#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstring>
#include <chrono>
#include <set>
#include <mutex>
#include "obfuscator.hpp"

// Global assertion counters
static std::atomic<uint64_t> g_pass_count{0};
static std::atomic<uint64_t> g_fail_count{0};

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        g_pass_count.fetch_add(1, std::memory_order_relaxed); \
    } else { \
        g_fail_count.fetch_add(1, std::memory_order_relaxed); \
        std::cerr << "[-] FAIL [Line " << __LINE__ << "]: " << msg << std::endl; \
    } \
} while(0)

// Macros under test at specific lines
#define TEST_STR_ALPHA "GhostAI_Alpha_SuperSecretKey_1234567890"
#define TEST_STR_BETA  "GhostAI_Beta_VeryLongConfigurationString_ExceedingSSOBoundary_abcdefghijklmnopqrstuvwxyz"
#define TEST_STR_GAMMA L"GhostAI_Gamma_Wide_Unicode_String_Test_0987654321"
#define TEST_STR_DELTA L"GhostAI_Delta_Wide_ExtendedString_ExceedingSSO_Length_Test_WideWideWideWide"

// -----------------------------------------------------------------------------
// Test 1: Address Identity & Race to Initialize
// -----------------------------------------------------------------------------
void test_concurrent_address_identity() {
    std::cout << "[TEST 1] Concurrent Race-to-Initialize & Address Identity..." << std::endl;

    constexpr int NUM_THREADS = 32;
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};

    std::vector<const char*> alpha_ptrs(NUM_THREADS, nullptr);
    std::vector<const char*> beta_ptrs(NUM_THREADS, nullptr);
    std::vector<const wchar_t*> gamma_ptrs(NUM_THREADS, nullptr);
    std::vector<const wchar_t*> delta_ptrs(NUM_THREADS, nullptr);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i, &start_flag, &alpha_ptrs, &beta_ptrs, &gamma_ptrs, &delta_ptrs]() {
            while (!start_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            // Simultaneous first-time or concurrent access
            alpha_ptrs[i] = _XC(TEST_STR_ALPHA);
            beta_ptrs[i] = _XC(TEST_STR_BETA);
            gamma_ptrs[i] = _XWC(TEST_STR_GAMMA);
            delta_ptrs[i] = _XWC(TEST_STR_DELTA);

            // Verify content correctness
            TEST_ASSERT(std::strcmp(alpha_ptrs[i], TEST_STR_ALPHA) == 0, "Alpha content matches");
            TEST_ASSERT(std::strcmp(beta_ptrs[i], TEST_STR_BETA) == 0, "Beta content matches");
            TEST_ASSERT(std::wcscmp(gamma_ptrs[i], TEST_STR_GAMMA) == 0, "Gamma content matches");
            TEST_ASSERT(std::wcscmp(delta_ptrs[i], TEST_STR_DELTA) == 0, "Delta content matches");
        });
    }

    // Release all threads simultaneously
    start_flag.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    // Crucial check: All threads MUST have received the EXACT SAME pointer address
    // because _XC points to a static storage duration object. If it were a temporary,
    // addresses would differ or be on the thread stack.
    const char* expected_alpha = alpha_ptrs[0];
    const char* expected_beta = beta_ptrs[0];
    const wchar_t* expected_gamma = gamma_ptrs[0];
    const wchar_t* expected_delta = delta_ptrs[0];

    for (int i = 1; i < NUM_THREADS; ++i) {
        TEST_ASSERT(alpha_ptrs[i] == expected_alpha, "All threads share identical Alpha address");
        TEST_ASSERT(beta_ptrs[i] == expected_beta, "All threads share identical Beta address");
        TEST_ASSERT(gamma_ptrs[i] == expected_gamma, "All threads share identical Gamma address");
        TEST_ASSERT(delta_ptrs[i] == expected_delta, "All threads share identical Delta address");
    }
}

// -----------------------------------------------------------------------------
// Test 2: Dangling Pointer Stress: Access Pointer After Thread Death
// -----------------------------------------------------------------------------
void test_post_thread_destruction_validity() {
    std::cout << "[TEST 2] Post-Thread Destruction Validity (Dangling Pointer Check)..." << std::endl;

    const char* captured_alpha = nullptr;
    const char* captured_beta = nullptr;
    const wchar_t* captured_gamma = nullptr;
    const wchar_t* captured_delta = nullptr;

    // Spawn thread, capture pointers returned by _XC / _XWC, and terminate thread
    {
        std::thread worker([&captured_alpha, &captured_beta, &captured_gamma, &captured_delta]() {
            captured_alpha = _XC("TemporaryThreadStringAlpha");
            captured_beta = _XC("TemporaryThreadStringBeta_ExceedingStandardSSOCapacity_1234567890");
            captured_gamma = _XWC(L"TemporaryThreadWideGamma");
            captured_delta = _XWC(L"TemporaryThreadWideDelta_ExceedingStandardSSOCapacity_0987654321");
        });
        worker.join();
    }
    // At this point, worker thread stack is completely destroyed and unmapped.

    // Access captured pointers from main thread
    TEST_ASSERT(captured_alpha != nullptr, "captured_alpha is not null");
    TEST_ASSERT(std::strcmp(captured_alpha, "TemporaryThreadStringAlpha") == 0,
                "captured_alpha remains valid after thread termination");

    TEST_ASSERT(captured_beta != nullptr, "captured_beta is not null");
    TEST_ASSERT(std::strcmp(captured_beta, "TemporaryThreadStringBeta_ExceedingStandardSSOCapacity_1234567890") == 0,
                "captured_beta remains valid after thread termination");

    TEST_ASSERT(captured_gamma != nullptr, "captured_gamma is not null");
    TEST_ASSERT(std::wcscmp(captured_gamma, L"TemporaryThreadWideGamma") == 0,
                "captured_gamma remains valid after thread termination");

    TEST_ASSERT(captured_delta != nullptr, "captured_delta is not null");
    TEST_ASSERT(std::wcscmp(captured_delta, L"TemporaryThreadWideDelta_ExceedingStandardSSOCapacity_0987654321") == 0,
                "captured_delta remains valid after thread termination");
}

// -----------------------------------------------------------------------------
// Test 3: High-Frequency Concurrent Read Stress (1,000,000 Iterations)
// -----------------------------------------------------------------------------
void test_high_frequency_concurrent_reads() {
    std::cout << "[TEST 3] High-Frequency Concurrent Read Stress (1,000,000+ invocations)..." << std::endl;

    static constexpr int NUM_THREADS = 16;
    static constexpr int ITERATIONS_PER_THREAD = 100000; // 1.6 million total calls

    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < ITERATIONS_PER_THREAD; ++i) {
                // Alternating accesses to multiple macros
                const std::string& refA = _X("LoopStringConstant_Alpha");
                const char* cstrA = _XC("LoopStringConstant_Alpha");
                const std::wstring& refW = _XW(L"LoopWideConstant_Beta");
                const wchar_t* cstrW = _XWC(L"LoopWideConstant_Beta");

                if (i % 25000 == 0) {
                    TEST_ASSERT(refA == "LoopStringConstant_Alpha", "refA content matches in loop");
                    TEST_ASSERT(std::strcmp(cstrA, "LoopStringConstant_Alpha") == 0, "cstrA content matches in loop");
                    TEST_ASSERT(refW == L"LoopWideConstant_Beta", "refW content matches in loop");
                    TEST_ASSERT(std::wcscmp(cstrW, L"LoopWideConstant_Beta") == 0, "cstrW content matches in loop");
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start_time).count();
    std::cout << "  [+] Completed " << (NUM_THREADS * ITERATIONS_PER_THREAD) 
              << " concurrent macro evaluations in " << elapsed << " ms." << std::endl;
}

// -----------------------------------------------------------------------------
// Test 4: Concurrency Under Heavy Heap Memory Churn
// -----------------------------------------------------------------------------
void test_concurrency_under_heap_churn() {
    std::cout << "[TEST 4] Concurrency under Heavy Heap Churn..." << std::endl;

    std::atomic<bool> churn_running{true};

    // Thread performing continuous heap churn
    std::thread churner([&churn_running]() {
        while (churn_running.load(std::memory_order_relaxed)) {
            std::vector<std::string> dummy;
            for (int j = 0; j < 500; ++j) {
                dummy.push_back("ChurnAllocation_" + std::to_string(j) + "_LongPaddingStringForHeapFragmentation");
            }
        }
    });

    // Worker threads reading _XC
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([]() {
            for (int k = 0; k < 50000; ++k) {
                const char* s = _XC("HeapChurnTestString_EnsuringNoHeapCorruption");
                if (k % 10000 == 0) {
                    TEST_ASSERT(std::strcmp(s, "HeapChurnTestString_EnsuringNoHeapCorruption") == 0,
                                "String intact under heap churn");
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    churn_running.store(false, std::memory_order_relaxed);
    churner.join();
}

// -----------------------------------------------------------------------------
// Main Entry Point
// -----------------------------------------------------------------------------
int main() {
    std::cout << "======================================================\n"
              << " GhostAI M2 String Obfuscator Concurrency Stress Test\n"
              << " Target: _X, _XC, _XW, _XWC Thread-Safety & Lifetimes\n"
              << "======================================================\n" << std::endl;

    test_concurrent_address_identity();
    test_post_thread_destruction_validity();
    test_high_frequency_concurrent_reads();
    test_concurrency_under_heap_churn();

    std::cout << "\n======================================================\n"
              << " String Macro Stress Test Results Summary:\n"
              << " PASSED: " << g_pass_count.load() << "\n"
              << " FAILED: " << g_fail_count.load() << "\n"
              << "======================================================\n" << std::endl;

    if (g_fail_count.load() == 0) {
        std::cout << "[+] ALL CONCURRENCY AND LIFETIME TESTS PASSED (100% SUCCESS)!\n";
        return 0;
    } else {
        std::cerr << "[-] CONCURRENCY STRESS TESTS FAILED WITH " << g_fail_count.load() << " ERRORS!\n";
        return 1;
    }
}
