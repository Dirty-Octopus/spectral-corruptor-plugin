// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace doctest {

struct TestCase
{
    const char* name;
    void (*func)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar (const char* name, void (*func)())
    {
        registry().push_back ({ name, func });
    }
};

namespace detail {

inline int& assertionFailures()
{
    static int failures = 0;
    return failures;
}

inline void check (bool expr, const char* exprText, const char* file, int line, bool fatal)
{
    if (expr)
        return;

    ++assertionFailures();
    std::cerr << file << ':' << line << ": " << (fatal ? "REQUIRE" : "CHECK")
              << '(' << exprText << ") failed\n";

    if (fatal)
        throw std::runtime_error ("require failed");
}

} // namespace detail

inline int runTests()
{
    const auto total = registry().size();
    int failedTests = 0;

    for (const auto& test : registry())
    {
        std::cout << "[RUN] " << test.name << std::endl;
        const int before = detail::assertionFailures();

        try
        {
            test.func();
        }
        catch (const std::exception& e)
        {
            ++failedTests;
            std::cerr << "[EXCEPTION] " << test.name << ": " << e.what() << '\n';
            continue;
        }

        if (detail::assertionFailures() != before)
        {
            ++failedTests;
            std::cerr << "[FAIL] " << test.name << '\n';
        }
        else
        {
            std::cout << "[OK] " << test.name << '\n';
        }
    }

    std::cout << (total - (std::vector<TestCase>::size_type) failedTests)
              << '/' << total << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}

} // namespace doctest

#define DOCTEST_CONCAT_IMPL(a, b) a##b
#define DOCTEST_CONCAT(a, b) DOCTEST_CONCAT_IMPL(a, b)
#define DOCTEST_TEST_CASE_IMPL(name, counter) \
    static void DOCTEST_CONCAT(test_func_, counter)(); \
    namespace { static ::doctest::Registrar DOCTEST_CONCAT(test_reg_, counter) (name, &DOCTEST_CONCAT(test_func_, counter)); } \
    static void DOCTEST_CONCAT(test_func_, counter)()

#define TEST_CASE(name) DOCTEST_TEST_CASE_IMPL (name, __COUNTER__)
#define CHECK(expr) ::doctest::detail::check ((expr), #expr, __FILE__, __LINE__, false)
#define REQUIRE(expr) ::doctest::detail::check ((expr), #expr, __FILE__, __LINE__, true)
