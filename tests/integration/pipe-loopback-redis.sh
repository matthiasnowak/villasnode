#!/bin/bash
#
# Integration loopback test for villas pipe.
#
# @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
# @copyright 2014-2021, Institute for Automation of Complex Power Systems, EONERC
# @license GNU General Public License (version 3)
#
# VILLASnode
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
##################################################################################

echo "Test not ready"
exit 99

set -e

DIR=$(mktemp -d)
pushd ${DIR}

function finish {
	popd
	rm -rf ${DIR}
}
trap finish EXIT
NUM_SAMPLES=${NUM_SAMPLES:-100}

villas signal -l ${NUM_SAMPLES} -n random > input.dat

FORMAT="protobuf"
VECTORIZE="10"
HOST="localhost"

if [ -n "${CI}" ]; then
	HOST="amqp"
fi

cat > config.json << EOF
{
	"nodes": {
		"node1": {
			"type": "redis",
			"format": "${FORMAT}",
			"vectorize": ${VECTORIZE},
		
			"uri": "tcp://${HOST}:6379/0"
		}
	}
}
EOF

villas pipe -l ${NUM_SAMPLES} config.json node1 > output.dat < input.dat

villas compare ${CMPFLAGS} input.dat output.dat