// Smoke test for the vendored GoogleTest wiring (Phase 1a / W6.2): proves the
// framework builds, links, runs, and reports a result through CTest. It asserts
// nothing about CODES itself — real unit tests (starting with the YAML config
// compiler) arrive in Phase 3. Runs serially; no MPI.
#include <gtest/gtest.h>

TEST(Smoke, ArithmeticSanity)
{
    EXPECT_EQ(1 + 1, 2);
}
