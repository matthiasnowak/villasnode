/** Timer/Counter unit test.
 *
 * @file
 * @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
 * @copyright 2017, Steffen Vogel
 * @license GNU General Public License (version 3)
 *
 * VILLASfpga
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *********************************************************************************/

#include <criterion/criterion.h>

#include "config.h"

#include <villas/log.hpp>
#include <villas/fpga/card.hpp>
#include <villas/fpga/ips/timer.hpp>

extern villas::fpga::PCIeCard* fpga;

Test(fpga, timer, .description = "Timer Counter")
{
	auto logger = loggerGetOrCreate("unittest:timer");

	for(auto& ip : fpga->ips) {
		// skip non-timer IPs
		if(*ip != villas::fpga::Vlnv("xilinx.com:ip:axi_timer:")) {
			continue;
		}

		logger->info("Testing {}", *ip);

		auto timer = reinterpret_cast<villas::fpga::ip::Timer&>(*ip);

		timer.start(timer.getFrequency() / 10);
		cr_assert(timer.wait(), "Timer failed");

		logger->info("Timer passed: {}", timer);
	}

	return;
}
