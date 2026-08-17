/*
 * BroTracker
 *
 * Description: Minimal dependency-free unit test framework for host tests.
 *
 * Copyright (C) smARTin and BroTracker contributors
 * License: GPL-3.0
 */

#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test
{
    struct TestCase
    {
        std::string name;
        std::function<void()> function;
    };

    inline std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    inline int& TotalFailureCount()
    {
        static int failures = 0;
        return failures;
    }

    inline int& CurrentCaseFailureCount()
    {
        static int failures = 0;
        return failures;
    }

    struct Registrar
    {
        Registrar(
            const std::string& name,
            std::function<void()> function)
        {
            Registry().push_back({name, std::move(function)});
        }
    };

    inline void ReportFailure(
        const char* expression,
        const char* file,
        int line)
    {
        std::cerr
            << "  FAILED: " << expression
            << " (" << file << ":" << line << ")\n";

        ++TotalFailureCount();
        ++CurrentCaseFailureCount();
    }

    // Runs every registered TEST_CASE and prints a pass/fail summary.
    inline int RunAll()
    {
        int total = 0;

        for (const auto& test_case : Registry())
        {
            ++total;
            CurrentCaseFailureCount() = 0;

            std::cout << "[RUN ] " << test_case.name << '\n';
            test_case.function();

            std::cout
                << (CurrentCaseFailureCount() == 0 ? "[ OK ] " : "[FAIL] ")
                << test_case.name << '\n';
        }

        std::cout
            << '\n' << total << " test case(s) run, "
            << TotalFailureCount() << " failure(s).\n";

        return TotalFailureCount() == 0 ? 0 : 1;
    }
}

#define TEST_CASE(name) \
    static void name(); \
    static const test::Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            test::ReportFailure(#expression, __FILE__, __LINE__); \
        } \
    } while (false)

#define CHECK_EQ(actual, expected) \
    do \
    { \
        if (!((actual) == (expected))) \
        { \
            test::ReportFailure(#actual " == " #expected, __FILE__, __LINE__); \
        } \
    } while (false)
