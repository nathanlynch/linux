// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for code related to the Partition Suspension Option as
 * specified in the Power Architecture® Platform Requirements+.
 */

#include <kunit/test.h>

static void this_should_fail(struct kunit *t)
{
	KUNIT_FAIL(t, "expected failure");
}

static struct kunit_case lpar_suspend_tests[] = {
	KUNIT_CASE(this_should_fail),
	{},
};

static struct kunit_suite lpar_suspend_tsuite = {
	.name = "pseries partition suspension",
	.test_cases = lpar_suspend_tests,
};

kunit_test_suites(&lpar_suspend_tsuite);
MODULE_LICENSE("GPL v2");
