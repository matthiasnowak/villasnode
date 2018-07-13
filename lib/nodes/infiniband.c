/** Node type: infiniband
 *
 * @author Dennis Potter <dennis@dennispotter.eu>
 * @copyright 2018, Institute for Automation of Complex Power Systems, EONERC
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

#include <string.h>
#include <math.h>

#include <villas/nodes/infiniband.h>
#include <villas/plugin.h>
#include <villas/utils.h>
#include <villas/format_type.h>
#include <villas/memory.h>
#include <villas/pool.h>
#include <villas/memory.h>
#include <villas/memory/ib.h>

int ib_disconnect(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	debug(LOG_IB | 1, "Starting to clean up");

	rdma_disconnect(ib->ctx.id);

	// Destroy QP
	rdma_destroy_qp(ib->ctx.id);
	debug(LOG_IB | 3, "Destroyed QP");

	// Deregister memory regions
	ibv_dereg_mr(ib->mem.mr_recv);
	if (ib->is_source)
		ibv_dereg_mr(ib->mem.mr_send);
	debug(LOG_IB | 3, "Deregistered memory regions");

	// Destroy pools
	pool_destroy(&ib->mem.p_recv);
	pool_destroy(&ib->mem.p_send);
	debug(LOG_IB | 3, "Destroyed memory pools");

	// Set available receive work requests to zero
	ib->conn.available_recv_wrs = 0;

	return ib->stopThreads;
}

int ib_post_recv_wrs(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	struct ibv_recv_wr wr, *bad_wr = NULL;
	int ret;
	struct ibv_sge sge;

	// Prepare receive Scatter/Gather element
	sge.addr = (uintptr_t) pool_get(&ib->mem.p_recv);
	sge.length = ib->mem.p_recv.blocksz;
	sge.lkey = ib->mem.mr_recv->lkey;

	// Prepare a receive Work Request
	wr.wr_id = (uintptr_t) sge.addr;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	// Post Work Request
	ret = ibv_post_recv(ib->ctx.id->qp, &wr, &bad_wr);

	return ret;
}

static void ib_build_ibv(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;

	debug(LOG_IB | 1, "Starting to build IBV components");

	// Create completion queues. No completion channel!)
	ib->ctx.recv_cq = ibv_create_cq(ib->ctx.id->verbs, ib->cq_size, NULL, NULL, 0);
	if (!ib->ctx.recv_cq)
		error("Could not create receive completion queue in node %s", node_name(n));

	debug(LOG_IB | 3, "Created receive Completion Queue");

	ib->ctx.send_cq = ibv_create_cq(ib->ctx.id->verbs, ib->cq_size, NULL, NULL, 0);
	if (!ib->ctx.send_cq)
		error("Could not create send completion queue in node %s", node_name(n));

	debug(LOG_IB | 3, "Created send Completion Queue");

	// Prepare remaining Queue Pair (QP) attributes
	ib->qp_init.send_cq = ib->ctx.send_cq;
	ib->qp_init.recv_cq = ib->ctx.recv_cq;

	// Create the actual QP
	ret = rdma_create_qp(ib->ctx.id, ib->ctx.pd, &ib->qp_init);
	if (ret)
		error("Failed to create Queue Pair in node %s", node_name(n));

	debug(LOG_IB | 3, "Created Queue Pair with %i receive and %i send elements",
		ib->qp_init.cap.max_recv_wr, ib->qp_init.cap.max_send_wr);

	// Allocate memory
	ib->mem.p_recv.state = STATE_DESTROYED;
	ib->mem.p_recv.queue.state = STATE_DESTROYED;

	// Set pool size to maximum size of Receive Queue
	pool_init(&ib->mem.p_recv, ib->qp_init.cap.max_recv_wr, SAMPLE_DATA_LEN(DEFAULT_SAMPLELEN), &memory_type_heap);
	if (ret)
		error("Failed to init recv memory pool of node %s: %s",
			node_name(n), gai_strerror(ret));

	debug(LOG_IB | 3, "Created internal receive pool with %i elements",
			ib->qp_init.cap.max_recv_wr);

	//ToDo: initialize r_addr_key struct if mode is RDMA

	// Register memory for IB Device. Not necessary if data is send
	// exclusively inline
	ib->mem.mr_recv = ibv_reg_mr(ib->ctx.pd,
				(char*)&ib->mem.p_recv+ib->mem.p_recv.buffer_off,
				ib->mem.p_recv.len,
				IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!ib->mem.mr_recv)
		error("Failed to register mr_recv with ibv_reg_mr of node %s",
			node_name(n));

	debug(LOG_IB | 3, "Registered receive pool with ibv_reg_mr");

	if (ib->is_source) {
		ib->mem.p_send.state = STATE_DESTROYED;
		ib->mem.p_send.queue.state = STATE_DESTROYED;

		// Set pool size to maximum size of Receive Queue
		pool_init(&ib->mem.p_send, ib->qp_init.cap.max_send_wr,	sizeof(double),	&memory_type_heap);
		if (ret)
			error("Failed to init send memory of node %s: %s", node_name(n), gai_strerror(ret));

		debug(LOG_IB | 3, "Created internal send pool with %i elements", ib->qp_init.cap.max_recv_wr);

		//ToDo: initialize r_addr_key struct if mode is RDMA

		// Register memory for IB Device. Not necessary if data is send
		// exclusively inline
		ib->mem.mr_send = ibv_reg_mr(ib->ctx.pd,
					(char*)&ib->mem.p_send+ib->mem.p_send.buffer_off,
					ib->mem.p_send.len,
					IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
		if (!ib->mem.mr_send)
			error("Failed to register mr_send with ibv_reg_mr of node %s", node_name(n));

		debug(LOG_IB | 3, "Registered send pool with ibv_reg_mr");
	}
}

static int ib_addr_resolved(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;

	debug(LOG_IB | 1, "Successfully resolved address");

	// Build all components from IB Verbs
	ib_build_ibv(n);

	// Resolve address
	ret = rdma_resolve_route(ib->ctx.id, ib->conn.timeout);
	if (ret)
		error("Failed to resolve route in node %s", node_name(n));

	return 0;
}

static int ib_route_resolved(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;

	struct rdma_conn_param cm_params;
	memset(&cm_params, 0, sizeof(cm_params));

	// Send connection request
	ret = rdma_connect(ib->ctx.id, &cm_params);
	if (ret)
		error("Failed to connect in node %s", node_name(n));

	debug(LOG_IB | 1, "Called rdma_connect");

	return 0;
}

static int ib_connect_request(struct node *n, struct rdma_cm_id *id)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;
	debug(LOG_IB | 1, "Received a connection request!");

	ib->ctx.id = id;
	ib_build_ibv(n);

	struct rdma_conn_param cm_params;
	memset(&cm_params, 0, sizeof(cm_params));

	// Accept connection request
	ret = rdma_accept(ib->ctx.id, &cm_params);
	if (ret)
		error("Failed to connect in node %s", node_name(n));

	info("Successfully accepted connection request in node %s", node_name(n));

	return 0;
}

int ib_reverse(struct node *n)
{
	return 0;
}

int ib_parse(struct node *n, json_t *cfg)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;

	int ret;
	char *local = NULL;
	char *remote = NULL;
	const char *port_space = "RDMA_PC_TCP";
	const char *poll_mode = "BUSY";
	const char *qp_type = "IBV_QPT_RC";
	int timeout = 1000;
	int cq_size = 128;
	int max_send_wr = 128;
	int max_recv_wr = 128;
	int max_inline_data = 0;
	int inline_mode = 1;

	json_error_t err;
	ret = json_unpack_ex(cfg, &err, 0, "{ s?: s, s?: s, s?: s, s?: i, \
					s?: s, s?: i, s?: s, s?: i, s?: i, \
					s?: i, s?: i}",
		"remote", &remote,
		"local", &local,
		"rdma_port_space", &port_space,
		"resolution_timeout", &timeout,
		"poll_mode", &poll_mode,
		"cq_size", &cq_size,
		"qp_type", &qp_type,
		"max_send_wr", &max_send_wr,
		"max_recv_wr", &max_recv_wr,
		"max_inline_data", &max_inline_data,
		"inline_mode", &inline_mode
	);
	if (ret)
		jerror(&err, "Failed to parse configuration of node %s", node_name(n));

	// Translate IP:PORT to a struct addrinfo
	char* ip_adr = strtok(local, ":");
	char* port = strtok(NULL, ":");

	//n->in.vectorize = 1024;
	//n->out.vectorize = 1024; //ToDo: make configurable

	ret = getaddrinfo(ip_adr, port, NULL, &ib->conn.src_addr);
	if (ret)
		error("Failed to resolve local address '%s' of node %s: %s",
			local, node_name(n), gai_strerror(ret));

	debug(LOG_IB | 4, "Translated %s:%s to a struct addrinfo in node %s", ip_adr, port, node_name(n));

	// Translate port space
	if (strcmp(port_space, "RDMA_PS_IPOIB") == 0)		ib->conn.port_space = RDMA_PS_IPOIB;
	else if (strcmp(port_space, "RDMA_PS_TCP") == 0) 	ib->conn.port_space = RDMA_PS_TCP;
	else if (strcmp(port_space, "RDMA_PS_UDP") == 0) 	ib->conn.port_space = RDMA_PS_UDP;
	else if (strcmp(port_space, "RDMA_PS_IB") == 0)  	ib->conn.port_space = RDMA_PS_IB;
	else
		error("Failed to translate rdma_port_space in node %s. %s is not a valid \
			port space supported by rdma_cma.h!", node_name(n), port_space);

	debug(LOG_IB | 4, "Translated %s to  enum rdma_port_space in node %s", port_space, node_name(n));

	// Set timeout
	ib->conn.timeout = timeout;

	debug(LOG_IB | 4, "Set  timeout to %i in node %s", timeout, node_name(n));

	// Translate poll mode
	if (strcmp(poll_mode, "EVENT") == 0)
		ib->poll.poll_mode = EVENT;
	else if (strcmp(poll_mode, "BUSY") == 0)
		ib->poll.poll_mode = BUSY;
	else
		error("Failed to translate poll_mode in node %s. %s is not a valid \
			poll mode!", node_name(n), poll_mode);

	debug(LOG_IB | 4, "Set poll mode to %s in node %s", poll_mode, node_name(n));

	// Set completion queue size
	ib->cq_size = cq_size;

	debug(LOG_IB | 4, "Set Completion Queue size to %i in node %s", cq_size, node_name(n));

	// Translate QP type
	if (strcmp(qp_type, "IBV_QPT_RC") == 0)		ib->qp_init.qp_type = IBV_QPT_RC;
	else if (strcmp(qp_type, "IBV_QPT_UC") == 0)	ib->qp_init.qp_type = IBV_QPT_UC;
	else if (strcmp(qp_type, "IBV_QPT_UD") == 0)	ib->qp_init.qp_type = IBV_QPT_UD;
	else
		error("Failed to translate qp_type in node %s. %s is not a valid \
			qp_type!", node_name(n), qp_type);

	debug(LOG_IB | 4, "Set Queue Pair type to %s in node %s", qp_type, node_name(n));

	// Translate inline mode
	ib->conn.inline_mode = inline_mode;

	debug(LOG_IB | 4, "Set inline_modee to %i in node %s", inline_mode, node_name(n));

	// Set max. send and receive Work Requests
	ib->qp_init.cap.max_send_wr = max_send_wr;
	ib->qp_init.cap.max_recv_wr = max_recv_wr;

	debug(LOG_IB | 4, "Set max_send_wr and max_recv_wr in node %s to %i and %i, respectively",
			node_name(n), max_send_wr, max_recv_wr);

	// Set available receive Work Requests to 0
	ib->conn.available_recv_wrs = 0;

	// Set remaining QP attributes
	ib->qp_init.cap.max_send_sge = 1;
	ib->qp_init.cap.max_recv_sge = 1;

	// Set number of bytes to be send inline
	ib->qp_init.cap.max_inline_data = max_inline_data;

	//Check if node is a source and connect to target
	if (remote) {
		debug(LOG_IB | 3, "Node %s is up as source and target", node_name(n));

		ib->is_source = 1;

		// Translate address info
		char* ip_adr = strtok(remote, ":");
		char* port = strtok(NULL, ":");

		ret = getaddrinfo(ip_adr, port, NULL, &ib->conn.dst_addr);
		if (ret)
			error("Failed to resolve remote address '%s' of node %s: %s",
				remote, node_name(n), gai_strerror(ret));

		debug(LOG_IB | 4, "Translated %s:%s to a struct addrinfo", ip_adr, port);
	}
	else {
		debug(LOG_IB | 3, "Node %s is set up as target", node_name(n));

		ib->is_source = 0;
	}

	return 0;
}

int ib_check(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;

	info("Starting check of node %s", node_name(n));

	// Check if the set value is a power of 2, and warn the user if this is not the case
	int max_send_pow = (int) pow(2, ceil(log2(ib->qp_init.cap.max_send_wr)));
	int max_recv_pow = (int) pow(2, ceil(log2(ib->qp_init.cap.max_recv_wr)));

	if (ib->qp_init.cap.max_send_wr != max_send_pow) {
		warn("Max nr. of send WRs (%i) is not a power of 2! It will be changed to a power of 2: %i",
			ib->qp_init.cap.max_send_wr, max_send_pow);

		// Change it now, because otherwise errors are possible in ib_start().
		ib->qp_init.cap.max_send_wr = max_send_pow;
	}

	if (ib->qp_init.cap.max_recv_wr != max_recv_pow) {
		warn("Max nr. of recv WRs (%i) is not a power of 2! It will be changed to a power of 2: %i",
			ib->qp_init.cap.max_recv_wr, max_recv_pow);

		// Change it now, because otherwise errors are possible in ib_start().
		ib->qp_init.cap.max_recv_wr = max_recv_pow;
	}

	// Check maximum size of max_recv_wr and max_send_wr
	if (ib->qp_init.cap.max_send_wr > 8192)
		warn("Max number of send WRs (%i) is bigger than send queue!", ib->qp_init.cap.max_send_wr);

	if (ib->qp_init.cap.max_recv_wr > 8192)
		warn("Max number of receive WRs (%i) is bigger than send queue!", ib->qp_init.cap.max_recv_wr);

	// Warn user if he changed the default inline value
	if (ib->qp_init.cap.max_inline_data != 0)
		warn("You changed the default value of max_inline_data. This might influence the maximum number of outstanding Work Requests in the Queue Pair and can be a reason for the Queue Pair creation to fail");

	info("Finished check of node %s", node_name(n));

	return 0;
}

char * ib_print(struct node *n)
{
	return 0;
}

int ib_destroy(struct node *n)
{
	return 0;
}

void * ib_rdma_cm_event_thread(void *n)
{
	struct node *node = (struct node *) n;
	struct infiniband *ib = (struct infiniband *) node->_vd;
	struct rdma_cm_event *event;
	int ret = 0;

	debug(LOG_IB | 1, "Started rdma_cm_event thread of node %s", node_name(node));

	// Wait until node is completely started
	while (node->state != STATE_STARTED);

	// Monitor event channel
	while (rdma_get_cm_event(ib->ctx.ec, &event) == 0) {

		switch(event->event) {
			case RDMA_CM_EVENT_ADDR_RESOLVED:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_ADDR_RESOLVED");

				ret = ib_addr_resolved(n);
				break;

			case RDMA_CM_EVENT_ADDR_ERROR:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_ADDR_ERROR");

				error("Address resolution (rdma_resolve_addr) failed!");
				break;

			case RDMA_CM_EVENT_ROUTE_RESOLVED:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_ROUTE_RESOLVED");

				ret = ib_route_resolved(n);
				break;

			case RDMA_CM_EVENT_ROUTE_ERROR:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_ROUTE_ERROR");

				error("Route resolution (rdma_resovle_route) failed!");
				break;

			case RDMA_CM_EVENT_CONNECT_REQUEST:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_CONNECT_REQUEST");

				ret = ib_connect_request(n, event->id);
				break;

			case RDMA_CM_EVENT_CONNECT_ERROR:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_CONNECT_ERROR");

				error("An error has occurred trying to establish a connection!");
				break;

			case RDMA_CM_EVENT_REJECTED:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_REJECTED");

				error("Connection request or response was rejected by the remote end point!");
				break;
			case RDMA_CM_EVENT_ESTABLISHED:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_ESTABLISHED");

				node->state = STATE_CONNECTED;

				info("Connection established in node %s", node_name(n));
				break;

			case RDMA_CM_EVENT_DISCONNECTED:
				debug(LOG_IB | 2, "Received RDMA_CM_EVENT_DISCONNECTED");

				node->state = STATE_STARTED;
				ret = ib_disconnect(n);

				info("Host disconnected. Ready to accept new connections.");

				break;

			case RDMA_CM_EVENT_TIMEWAIT_EXIT:
				break;

			default:
				error("Unknown event occurred: %u", event->event);
		}

		rdma_ack_cm_event(event);

		if (ret)
			break;
	}

	return NULL;
}

int ib_start(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;

	debug(LOG_IB | 1, "Started ib_start");

	// Create event channel
	ib->ctx.ec = rdma_create_event_channel();
	if (!ib->ctx.ec)
		error("Failed to create event channel in node %s!", node_name(n));

	debug(LOG_IB | 3, "Created event channel");

	ret = rdma_create_id(ib->ctx.ec, &ib->ctx.id, NULL, ib->conn.port_space);
	if (ret)
		error("Failed to create rdma_cm_id of node %s: %s", node_name(n), gai_strerror(ret));

	debug(LOG_IB | 3, "Created rdma_cm_id");

	// Bind rdma_cm_id to the HCA
	ret = rdma_bind_addr(ib->ctx.id, ib->conn.src_addr->ai_addr);
	if (ret)
		error("Failed to bind to local device of node %s: %s",
			node_name(n), gai_strerror(ret));

	debug(LOG_IB | 3, "Bound rdma_cm_id to Infiniband device");

	// The ID will be overwritten for the target. If the event type is
	// RDMA_CM_EVENT_CONNECT_REQUEST, >then this references a new id for
	// that communication.
	ib->ctx.listen_id = ib->ctx.id;

	// Initialize send Work Completion stack
	ib->conn.send_wc_stack.top = 0;
	ib->conn.send_wc_stack.array = alloc(ib->qp_init.cap.max_recv_wr * sizeof(uint64_t) );

	debug(LOG_IB | 3, "Initialized Work Completion Stack");

	if (ib->is_source) {
		// Resolve address
		ret = rdma_resolve_addr(ib->ctx.id, NULL, ib->conn.dst_addr->ai_addr, ib->conn.timeout);
		if (ret)
			error("Failed to resolve remote address after %ims of node %s: %s",
				ib->conn.timeout, node_name(n), gai_strerror(ret));
	}
	else {
		// Listen on rdma_cm_id for events
		ret = rdma_listen(ib->ctx.listen_id, 10);
		if (ret)
			error("Failed to listen to rdma_cm_id on node %s", node_name(n));

		debug(LOG_IB | 3, "Started to listen to rdma_cm_id");
	}

	//Allocate protection domain
	ib->ctx.pd = ibv_alloc_pd(ib->ctx.id->verbs);
	if (!ib->ctx.pd)
		error("Could not allocate protection domain in node %s", node_name(n));

	debug(LOG_IB | 3, "Allocated Protection Domain");


	// Several events should occur on the event channel, to make
	// sure the nodes are succesfully connected.
	debug(LOG_IB | 1, "Starting to monitor events on rdma_cm_id");

	//Create thread to monitor rdma_cm_event channel
	ret = pthread_create(&ib->conn.rdma_cm_event_thread, NULL, ib_rdma_cm_event_thread, n);
	if (ret)
		error("Failed to create thread to monitor rdma_cm events in node %s: %s",
			node_name(n), gai_strerror(ret));

	return 0;
}

int ib_stop(struct node *n)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	int ret;

	debug(LOG_IB | 1, "Called ib_stop");

	ib->stopThreads = 1;

	// Call RDMA disconnect function
	// Will flush all outstanding WRs to the Completion Queue and
	// will call RDMA_CM_EVENT_DISCONNECTED if that is done.
	if (n->state == STATE_CONNECTED)
		ret = rdma_disconnect(ib->ctx.id);
	else
		ret = rdma_disconnect(ib->ctx.listen_id);
	if (ret)
		error("Error while calling rdma_disconnect in node %s: %s",
			node_name(n), gai_strerror(ret));

	debug(LOG_IB | 3, "Called rdma_disconnect");
	info("Disconnecting... Please give me a few seconds.");

	// Wait for event thread to join
	ret = pthread_join(ib->conn.rdma_cm_event_thread, NULL);
	if (ret)
		error("Error while joining rdma_cm_event_thread in node %s: %i", node_name(n), ret);

	debug(LOG_IB | 3, "Joined rdma_cm_event_thread");

	// Destroy RDMA CM ID
	rdma_destroy_id(ib->ctx.id);
	debug(LOG_IB | 3, "Destroyed rdma_cm_id");

	// Dealloc Protection Domain
	ibv_dealloc_pd(ib->ctx.pd);
	debug(LOG_IB | 3, "Destroyed protection domain");

	// Destroy event channel
	rdma_destroy_event_channel(ib->ctx.ec);
	debug(LOG_IB | 3, "Destroyed event channel");

	info("Successfully stopped %s", node_name(n));

	return 0;
}

int ib_init(struct super_node *n)
{
	return 0;
}

int ib_deinit()
{
	return 0;
}

int ib_read(struct node *n, struct sample *smps[], unsigned cnt, unsigned *release)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	struct ibv_wc wc[cnt];
	struct ibv_recv_wr wr[cnt], *bad_wr = NULL;
    	struct ibv_sge sge[cnt];
	struct ibv_mr *mr;
	int ret = 0, wcs = 0, read_values = 0, max_wr_post;

	debug(LOG_IB | 15, "ib_read is called");

	if (n->state == STATE_CONNECTED) {

		max_wr_post = cnt;

		// Poll Completion Queue
		// If we've already posted enough receive WRs, try to pull cnt
		if (ib->conn.available_recv_wrs > ( ib->qp_init.cap.max_recv_wr - (1024 * n->in.vectorize) ) ) { //ToDo: Make configurable
			while(1) {
				wcs = ibv_poll_cq(ib->ctx.recv_cq, cnt, wc);
				if (wcs) {
					debug(LOG_IB | 10, "Received %i Work Completions", wcs);

					read_values = wcs; // Value to return
					max_wr_post = wcs; // Make space free in smps[]

					break;
				}
			}

			// All samples (wcs * received + unposted) should be released. Let
			// *release be equal to allocated.
			//
			// This is set in the framework, before this function was called
		}
		else {
			ib->conn.available_recv_wrs += max_wr_post;
			*release = 0; // While we fill the receive queue, we always use all samples
		}

		// Get Memory Region
		mr = memory_ib_get_mr(smps[0]);

		for (int i = 0; i < max_wr_post; i++) {
			// Prepare receive Scatter/Gather element
    			sge[i].addr = (uint64_t) &smps[i]->data;
    			sge[i].length = SAMPLE_DATA_LEN(DEFAULT_SAMPLELEN);
    			sge[i].lkey = mr->lkey;

    			// Prepare a receive Work Request
    			wr[i].wr_id = (uintptr_t) smps[i];
			wr[i].next = &wr[i+1];
    			wr[i].sg_list = &sge[i];
    			wr[i].num_sge = 1;
		}

		wr[max_wr_post-1].next = NULL;

		debug(LOG_IB | 5, "Prepared %i new receive Work Requests", max_wr_post);
		debug(LOG_IB | 5, "%i receive Work Requests in Receive Queue", ib->conn.available_recv_wrs);

		// Post list of Work Requests
		ret = ibv_post_recv(ib->ctx.id->qp, &wr[0], &bad_wr);

		if (ret)
			error("Was unable to post receive WR in node %s: %i, bad WR ID: 0x%lx",
			node_name(n), ret, bad_wr->wr_id);

		debug(LOG_IB | 10, "Succesfully posted receive Work Requests");

		// Doesn't start, if wcs == 0
		for (int j = 0; j < wcs; j++) {
			if ( !( (wc[j].opcode & IBV_WC_RECV) && wc[j].status == IBV_WC_SUCCESS) )
				read_values--;

			if (wc[j].status == IBV_WC_WR_FLUSH_ERR)
				debug(LOG_IB | 5, "Received IBV_WC_WR_FLUSH_ERR (ib_read). Ignore it.");
			else if (wc[j].status != IBV_WC_SUCCESS)
				warn("Work Completion status was not IBV_WC_SUCCES in node %s: %i",
					node_name(n), wc[j].status);

			smps[j] = (struct sample *) (wc[j].wr_id);
			smps[j]->length = wc[j].byte_len / sizeof(double);
		}
	}
	return read_values;
}

int ib_write(struct node *n, struct sample *smps[], unsigned cnt, unsigned *release)
{
	struct infiniband *ib = (struct infiniband *) n->_vd;
	struct ibv_send_wr wr[cnt], *bad_wr = NULL;
	struct ibv_sge sge[cnt];
	struct ibv_wc wc[cnt];
	struct ibv_mr *mr;

	int ret;
	int sent = 0; //Used for first loop: prepare work requests to post to send queue

	debug(LOG_IB | 10, "ib_write is called");

	*release = 0;

	if (n->state == STATE_CONNECTED) {
		// First, write

		// Get Memory Region
		mr = memory_ib_get_mr(smps[0]);

		for (sent = 0; sent < cnt; sent++) {
			// Set Scatter/Gather element to data of sample
			sge[sent].addr = (uint64_t) &smps[sent]->data;
			sge[sent].length = smps[sent]->length*sizeof(double);
			sge[sent].lkey = mr->lkey;

			// Check if data can be send inline
			int send_inline = (sge[sent].length < ib->qp_init.cap.max_inline_data) ?
				(ib->conn.inline_mode > 0) : 0;

			debug(LOG_IB | 10, "Sample will be send inline [0/1]: %i", send_inline);

			// Set Send Work Request
			wr[sent].wr_id = send_inline ? 0 : (uintptr_t) smps[sent]; // This way the sample can be release in WC
			wr[sent].sg_list = &sge[sent];
			wr[sent].num_sge = 1;

			if (sent == (cnt-1)) {
				debug(LOG_IB | 10, "Prepared %i send Work Requests", (sent+1));
				wr[sent].next = NULL;
			}
			else
				wr[sent].next = &wr[sent+1];

			wr[sent].send_flags = IBV_SEND_SIGNALED | (send_inline << 3);
			wr[sent].imm_data = htonl(0); //ToDo: set this to a useful value
			wr[sent].opcode = IBV_WR_SEND_WITH_IMM;
		}

		// Send linked list of Work Requests
		ret = ibv_post_send(ib->ctx.id->qp, wr, &bad_wr);
		debug(LOG_IB | 4, "Posted send Work Requests");

		// Reorder list. Place inline and unposted samples to the top
		// m will always be equal or smaller than *release
		for (int m = 0; m < cnt; m++) {
			// We can't use wr_id as identifier, since it is 0 for inline
			// elements
			if (ret && (wr[m].sg_list == bad_wr->sg_list)) {
				// The remaining work requests will be bad. Ripple through list
				// and prepare them to be released
				debug(LOG_IB | 4, "Bad WR occured with ID: 0x%lx and S/G address: 0x%px: %i",
						bad_wr->wr_id, bad_wr->sg_list, ret);

				while (1) {
					smps[*release] = smps[m];

					(*release)++; // Increment number of samples to be released
					sent--; // Decrement the number of succesfully posted elements

					if (++m == cnt) break;
				}
			}
			else if (wr[m].send_flags & IBV_SEND_INLINE) {
				smps[*release] = smps[m];

				(*release)++;
			}

		}

		debug(LOG_IB | 4, "%i samples will be released", *release);

		// Always poll cnt items from Receive Queue. If there is not enough space in
		// smps, we temporarily save it on a stack
		ret = ibv_poll_cq(ib->ctx.send_cq, cnt, wc);

		for (int i = 0; i < ret; i++) {
			if (wc[i].status != IBV_WC_SUCCESS && wc[i].status != IBV_WC_WR_FLUSH_ERR)
				warn("Work Completion status was not IBV_WC_SUCCES in node %s: %i",
					node_name(n), wc[i].status);

			// Release only samples which were not send inline
			if (wc[i].wr_id) {
				if (cnt - *release > 0) {
					smps[*release] = (struct sample *) (wc[i].wr_id);
					(*release)++;
				}
				else {
					ib->conn.send_wc_stack.array[ib->conn.send_wc_stack.top] = wc[i].wr_id;
					ib->conn.send_wc_stack.top++;
				}
			}
		}

		// Check if we still have some space and try to get rid of some addresses on our stack
		if (ib->conn.send_wc_stack.top > 0) {
			int empty_smps = cnt - *release;
			for (int i = 0; i < empty_smps; i++) {
				ib->conn.send_wc_stack.top--;

				smps[*release] = (struct sample *) ib->conn.send_wc_stack.array[ib->conn.send_wc_stack.top];

				(*release)++;

				if(ib->conn.send_wc_stack.top == 0) break;
			}
		}

	}

	return sent;
}

int ib_fd(struct node *n)
{
	return 0;
}

static struct plugin p = {
	.name           = "infiniband",
	.description    = "Infiniband",
	.type           = PLUGIN_TYPE_NODE,
	.node           = {
		.vectorize      = 0,
		.size           = sizeof(struct infiniband),
		.reverse        = ib_reverse,
		.parse          = ib_parse,
		.check		= ib_check,
		.print          = ib_print,
		.start          = ib_start,
		.destroy        = ib_destroy,
		.stop           = ib_stop,
		.init           = ib_init,
		.deinit         = ib_deinit,
		.read           = ib_read,
		.write          = ib_write,
		.fd             = ib_fd,
		.memory_type    = memory_ib
	}
};

REGISTER_PLUGIN(&p)
LIST_INIT_STATIC(&p.node.instances)
