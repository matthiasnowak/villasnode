/** The API ressource for start/stop/pause/resume nodes.
 *
 * @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
 * @copyright 2014-2020, Institute for Automation of Complex Power Systems, EONERC
 * @license GNU General Public License (version 3)
 *
 * VILLASnode
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

#include <jansson.h>

#include <villas/plugin.h>
#include <villas/node.h>
#include <villas/super_node.hpp>
#include <villas/utils.hpp>
#include <villas/api.hpp>
#include <villas/api/session.hpp>
#include <villas/api/request.hpp>
#include <villas/api/response.hpp>

namespace villas {
namespace node {
namespace api {

template<int (*A)(struct node *)>
class NodeActionRequest : public Request  {

public:
	using Request::Request;

	virtual Response * execute()
	{
		if (method != Method::POST)
			throw InvalidMethod(this);

		if (body != nullptr)
			throw BadRequest("Node endpoints do not accept any body data");

		const auto &nodeName = matches[1].str();

		struct vlist *nodes = session->getSuperNode()->getNodes();
		struct node *n = (struct node *) vlist_lookup(nodes, nodeName.c_str());

		if (!n)
			throw Error(HTTP_STATUS_NOT_FOUND, "Node not found");

		A(n);

		return new Response(session);
	}

};

/* Register API requests */
static char n1[] = "node/start";
static char r1[] = "/node/([^/]+)/start";
static char d1[] = "start a node";
static RequestPlugin<NodeActionRequest<node_start>, n1, r1, d1> p1;

static char n2[] = "node/stop";
static char r2[] = "/node/([^/]+)/stop";
static char d2[] = "stop a node";
static RequestPlugin<NodeActionRequest<node_stop>, n2, r2, d2> p2;

static char n3[] = "node/pause";
static char r3[] = "/node/([^/]+)/pause";
static char d3[] = "pause a node";
static RequestPlugin<NodeActionRequest<node_pause>, n3, r3, d3> p3;

static char n4[] = "node/resume";
static char r4[] = "/node/([^/]+)/resume";
static char d4[] = "resume a node";
static RequestPlugin<NodeActionRequest<node_resume>, n4, r4, d4> p4;

static char n5[] = "node/restart";
static char r5[] = "/node/([^/]+)/restart";
static char d5[] = "restart a node";
static RequestPlugin<NodeActionRequest<node_restart>, n5, r5, d5> p5;


} /* namespace api */
} /* namespace node */
} /* namespace villas */
