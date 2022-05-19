/** Unit tests for utilities
 *
 * @author Steffen Vogel <svogel2@eonerc.rwth-aachen.de>
 * @copyright 2014-2022, Institute for Automation of Complex Power Systems, EONERC
 * @license Apache License 2.0
 *********************************************************************************/

#include <criterion/criterion.h>

#include <villas/colors.hpp>
#include <villas/utils.hpp>
#include <villas/version.hpp>
#include <villas/cpuset.hpp>
#include <villas/log.hpp>

using namespace villas::utils;

// cppcheck-suppress unknownMacro
TestSuite(utils, .description = "Utilities");

/* Simple normality test for 1,2,3s intervals */
Test(utils, box_muller)
{
	double n;
	unsigned sigma[3] = { 0 };
	unsigned iter = 1000000;

	for (unsigned i = 0; i < iter; i++) {
		n = boxMuller(0, 1);

		if      (n > 2 || n < -2) sigma[2]++;
		else if (n > 1 || n < -1) sigma[1]++;
		else                     sigma[0]++;
	}

#if 0
	printf("%f %f %f\n",
		(double) sigma[2] / iter,
		(double) sigma[1] / iter,
		(double) sigma[0] / iter);
#endif

	/* The random variable generated by the Box Muller transform is
	 * not an ideal normal distributed variable.
	 * The numbers from below are empirically measured. */
	cr_assert_float_eq((double) sigma[2] / iter, 0.045527, 1e-2);
	cr_assert_float_eq((double) sigma[1] / iter, 0.271644, 1e-2);
	cr_assert_float_eq((double) sigma[0] / iter, 0.682829, 1e-2);
}

#ifdef __linux__
Test(utils, cpuset)
{
	using villas::utils::CpuSet;

	uintmax_t int1 = 0x1234567890ABCDEFULL;

	CpuSet cset1(int1);

	std::string cset1_str = cset1;

	CpuSet cset2(cset1_str);

	cr_assert_eq(cset1, cset2);

	uintmax_t int2 = cset2;

	cr_assert_eq(int1, int2);

	CpuSet cset3("1-5");
	CpuSet cset4("1,2,3,4,5");

	cr_assert_eq(cset3, cset4);
	cr_assert_eq(cset3.count(), 5);

	cr_assert(cset3.isSet(3));
	cr_assert_not(cset3.isSet(6));

	cr_assert(cset3[3]);
	cr_assert_not(cset3[6]);

	cset4.set(6);
	cr_assert(cset4[6]);

	cset4.clear(6);
	cr_assert_not(cset4[6]);

	cr_assert_str_eq(static_cast<std::string>(cset4).c_str(), "1-5");

	cr_assert_any_throw(CpuSet cset5("0-"));

	CpuSet cset6;
	cr_assert(cset6.empty());
	cr_assert_eq(cset6.count(), 0);

	cr_assert((~cset6).full());
	cr_assert((cset1 | ~cset1).full());
	cr_assert((cset1 ^ cset1).empty());
	cr_assert((cset1 & cset6).empty());

	cset1.zero();
	cr_assert(cset1.empty());
}
#endif /* __linux__ */

Test(utils, memdup)
{
	char orig[1024], *copy;
	size_t len;

	len = readRandom(orig, sizeof(orig));
	cr_assert_eq(len, sizeof(orig));

	copy = (char *) memdup(orig, sizeof(orig));
	cr_assert_not_null(copy);
	cr_assert_arr_eq(copy, orig, sizeof(orig));

	free(copy);
}

Test(utils, is_aligned)
{
	/* Positive */
	cr_assert(IS_ALIGNED(1, 1));
	cr_assert(IS_ALIGNED(128, 64));

	/* Negative */
	cr_assert(!IS_ALIGNED(55, 16));
	cr_assert(!IS_ALIGNED(55, 55));
	cr_assert(!IS_ALIGNED(1128, 256));
}

Test(utils, ceil)
{
	cr_assert_eq(CEIL(10, 3), 4);
	cr_assert_eq(CEIL(10, 5), 2);
	cr_assert_eq(CEIL(4, 3), 2);
}

Test(utils, is_pow2)
{
	/* Positive */
	cr_assert(IS_POW2(1));
	cr_assert(IS_POW2(2));
	cr_assert(IS_POW2(64));

	/* Negative */
	cr_assert(!IS_POW2(0));
	cr_assert(!IS_POW2(3));
	cr_assert(!IS_POW2(11111));
	cr_assert(!IS_POW2(-1));
}

Test(utils, strf)
{
	char *buf = nullptr;

	buf = strcatf(&buf, "Hallo %s", "Steffen.");
	cr_assert_str_eq(buf, "Hallo Steffen.");

	strcatf(&buf, " Its Monday %uth %s %u.", 13, "August", 2018);
	cr_assert_str_eq(buf, "Hallo Steffen. Its Monday 13th August 2018.");

	free(buf);
}

Test(utils, version)
{
	using villas::utils::Version;

	Version v1 = Version("1.2");
	Version v2 = Version("1.3");
	Version v3 = Version("55");
	Version v4 = Version("66");
	Version v5 = Version(66);
	Version v6 = Version(1, 2, 5);
	Version v7 = Version("1.2.5");

	cr_assert_lt(v1, v2);
	cr_assert_eq(v1, v1);
	cr_assert_gt(v2, v1);
	cr_assert_lt(v3, v4);
	cr_assert_eq(v4, v5);
	cr_assert_eq(v6, v7);
}

Test(utils, sha1sum)
{
	int ret;
	FILE *f = tmpfile();

	unsigned char     hash[SHA_DIGEST_LENGTH];
	unsigned char expected[SHA_DIGEST_LENGTH] = { 0x69, 0xdf, 0x29, 0xdf, 0x1f, 0xf2, 0xd2, 0x5d, 0xb8, 0x68, 0x6c, 0x02, 0x8d, 0xdf, 0x40, 0xaf, 0xb3, 0xc1, 0xc9, 0x4d };

	/* Write the first 512 fibonaccia numbers to the file */
	for (int i = 0, a = 0, b = 1, c; i < 512; i++, a = b, b = c) {
		c = a + b;

		fwrite((void *) &c, sizeof(c), 1, f);
	}

	ret = sha1sum(f, hash);

	cr_assert_eq(ret, 0);
	cr_assert_arr_eq(hash, expected, SHA_DIGEST_LENGTH);

	fclose(f);
}

Test(utils, decolor)
{
	char str[] = "This " CLR_RED("is") " a " CLR_BLU("colored") " " CLR_BLD("text!");
	char expect[] = "This is a colored text!";

	decolor(str);

	cr_assert_str_eq(str, expect);
}
